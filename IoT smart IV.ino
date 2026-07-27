#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
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
#define PIN_BTN_NEXT 25
#define PIN_BTN_ADJUST 26
#define PIN_BTN_RESET 27  // Not used - disabled

// ============================================================
// SYSTEM CONFIGURATION
// ============================================================
float calibrationFactor = 435.5;
float emptyWeightGrams = 30.0;
float fullWeightGrams = 500.0;
int lowThresholdPct = 40;
int criticalThresholdPct = 15;

enum Status { STATUS_NORMAL, STATUS_LOW, STATUS_CRITICAL };
Status currentStatus = STATUS_NORMAL;
int bedID = 1;

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
const unsigned long UPLOAD_MS = 20000;  // Upload every 20 seconds

// ============================================================
// HARDWARE OBJECTS
// ============================================================
HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
WiFiClient client;

byte barBlock[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};

unsigned long lastBtnNext = 0;
unsigned long lastBtnAdjust = 0;
const unsigned long DEBOUNCE_MS = 250;

float currentWeightGrams = 0;
int currentPercent = 100;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("--- Smart IV Fluid Monitoring System ---");

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_ADJUST, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  // Initialize LCD
  Wire.begin(21, 22); 
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, barBlock);
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("IV Bed Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Init Hardware...");

  // Setup scale
  scale.begin(HX711_DT, HX711_SCK);
  if (scale.wait_ready_timeout(1000)) {
    scale.set_scale(calibrationFactor);
    scale.tare(); 
    Serial.println("Scale initialized");
  } else {
    Serial.println("WARNING: Scale not responding!");
  }

  // Connect to WiFi
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi ");
  WiFi.begin(ssid, password);
  
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    digitalWrite(PIN_LED_GREEN, HIGH);
    delay(250);
    digitalWrite(PIN_LED_GREEN, LOW);
    delay(250);
    Serial.print(".");
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed! Operating offline.");
  }

  ThingSpeak.begin(client);
  
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi: Connected" : "WiFi: OFFLINE");
  delay(1000);
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
  
  // Upload to ThingSpeak every 20 seconds
  if (millis() - lastUpload >= UPLOAD_MS) {
    lastUpload = millis();
    if (WiFi.status() == WL_CONNECTED) {
      uploadToThingSpeak();
    } else {
      Serial.println("Skipping upload: WiFi offline.");
    }
  }

  delay(100);
}

// ============================================================
// SENSOR FUNCTIONS
// ============================================================
void readWeight() {
  if (scale.wait_ready_timeout(80)) {
    currentWeightGrams = scale.get_units(5);  // Average of 5 readings
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
  // Turn off all LEDs
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  // Set correct LED based on status
  if (currentStatus == STATUS_NORMAL) {
    digitalWrite(PIN_LED_GREEN, HIGH);
  } else if (currentStatus == STATUS_LOW) {
    digitalWrite(PIN_LED_BLUE, HIGH);
  } else if (currentStatus == STATUS_CRITICAL) {
    digitalWrite(PIN_LED_RED, HIGH);
  }
}

// ============================================================
// LCD DISPLAY FUNCTIONS
// ============================================================
void updateLCD() {
  lcd.clear();
  
  switch (currentScreen) {
    case HOME: {
      // Line 1: Bed ID, Status, and Weight
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
      
      // Line 2: Percentage and progress bar
      lcd.setCursor(0, 1);
      lcd.print(currentPercent);
      lcd.print("% ");
      printBar(currentPercent);
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

  // NEXT Button - Cycle through menus
  if (digitalRead(PIN_BTN_NEXT) == LOW && now - lastBtnNext > DEBOUNCE_MS) {
    lastBtnNext = now;
    currentScreen = (MenuScreen)((currentScreen + 1) % 4);
    lcd.clear(); 
    Serial.print("Menu: ");
    Serial.println(currentScreen);
  }

  // ADJUST Button - Change values based on current menu
  if (digitalRead(PIN_BTN_ADJUST) == LOW && now - lastBtnAdjust > DEBOUNCE_MS) {
    lastBtnAdjust = now;
    switch (currentScreen) {
      case SET_LOW:
        lowThresholdPct += 5;
        if (lowThresholdPct > 90) lowThresholdPct = 5;
        Serial.print("Low Threshold: ");
        Serial.println(lowThresholdPct);
        break;
      case SET_CRITICAL:
        criticalThresholdPct += 5;
        if (criticalThresholdPct > 90) criticalThresholdPct = 5;
        Serial.print("Critical Threshold: ");
        Serial.println(criticalThresholdPct);
        break;
      case SET_BED:
        bedID++;
        if (bedID > 20) bedID = 1;
        Serial.print("Bed ID: ");
        Serial.println(bedID);
        break;
      default:
        break;
    }
  }
}

// ============================================================
// THINGSPEAK UPLOAD FUNCTION
// ============================================================
void uploadToThingSpeak() {
  // Set all fields
  ThingSpeak.setField(1, currentPercent);        // GAUGE Widget
  ThingSpeak.setField(2, (int)currentStatus);    // LAMP Indicator (0=Normal,1=Low,2=Critical)
  ThingSpeak.setField(3, bedID);                 // Bed ID
  ThingSpeak.setField(4, currentWeightGrams);    // NUMERIC DISPLAY (Weight in grams)
  
  Serial.println("\n=== UPLOADING TO THINGSPEAK ===");
  Serial.println("Field 1 (Percent): " + String(currentPercent) + "% ← GAUGE");
  Serial.println("Field 2 (Status): " + String((int)currentStatus) + " ← LAMP (0=Normal,1=Low,2=Critical)");
  Serial.println("Field 3 (Bed ID): " + String(bedID));
  Serial.println("Field 4 (Weight): " + String(currentWeightGrams) + "g ← NUMERIC DISPLAY");
  
  int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  
  Serial.print("Response Code: ");
  Serial.println(code);
  
  if (code == 200) {
    Serial.println("✓ SUCCESS! Data uploaded to ThingSpeak");
  } else {
    Serial.println("✗ UPLOAD FAILED! Error: " + String(code));
    if (code == 401) Serial.println("  → Check Write API Key!");
    if (code == 404) Serial.println("  → Check Channel ID!");
    if (code == 429) Serial.println("  → Rate limited! Wait longer.");
  }
}