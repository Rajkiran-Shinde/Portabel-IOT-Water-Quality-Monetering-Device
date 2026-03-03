#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h> 
#include <Preferences.h>
#include <AsyncJson.h> 

#include "config.h"
#include "temp.h"
#include "tds.h"
#include "ph.h"
#include "firebase.h"
#include "alerts.h"

// --- Global Objects ---
Adafruit_ADS1115 ads;
PHSensor phSensor(&ads, PH_ADC_CHANNEL);
TempSensor waterTemp(ONE_WIRE_BUS);
TDSSensor tdsSensor(&ads, TDS_ADC_CHANNEL);
FirebaseManager database;
AlertSystem alerts(BUZZER_PIN);

// --- Web Server & Global Data Variables ---
AsyncWebServer server(80);

// Globals to share data between Core 1 (Sensors) and Core 0 (Web UI)
float live_ph = 0.0;
float live_tds = 0.0;
float live_temp = 0.0;
float live_hardness = 0.0;

// FreeRTOS Task Handles
TaskHandle_t WebTask;
TaskHandle_t SensorTask;

// ==========================================
// --- DYNAMIC SETTINGS & MEMORY ---
// ==========================================
String twilio_sid = "";
String twilio_token = "";
String twilio_from = "";
String twilio_to = "";
String twilio_msg = "WATER STATUS: ";
unsigned long routine_sms_interval = 14400000; // Default 4 hours
unsigned long danger_sms_interval = 3600000;   // Default 1 hour
unsigned long fb_update_interval = 300000;     // Default 5 mins

Preferences preferences;

// Function to load saved settings from Flash Memory on boot
void loadSettings() {
  preferences.begin("wqi-app", false); 
  twilio_sid = preferences.getString("tw_sid", "");
  twilio_token = preferences.getString("tw_token", "");
  twilio_from = preferences.getString("tw_from", "");
  twilio_to = preferences.getString("tw_to", "");
  twilio_msg = preferences.getString("tw_msg", "WATER STATUS: ");
  
  routine_sms_interval = preferences.getULong("tw_safe_int", 14400000);
  danger_sms_interval = preferences.getULong("tw_danger_int", 3600000);
  fb_update_interval = preferences.getULong("fb_int", 300000);
  preferences.end();
  Serial.println("System Settings Loaded from Flash Memory.");
}

// ==========================================
// CORE 0: WEB SERVER & WIFI TASK
// ==========================================
void webTaskCode(void * parameter) {
  // Wait for WiFi to connect (handled by Core 1's database.begin())
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  // 1. Initialize LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("Error mounting LittleFS! Did you upload the Filesystem Image?");
    vTaskDelete(NULL);
  }

  // 2. Initialize mDNS
  if (!MDNS.begin("wqi")) { 
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started. Access via http://wqi.local");
  }

  // 3. Serve the static HTML/CSS/JS files
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // 4. API Endpoint: Send Live Data to the Web Dashboard
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<200> doc;
    doc["ph"] = live_ph;
    doc["tds"] = live_tds;
    doc["temp"] = live_temp;
    doc["hardness"] = live_hardness;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 5. API Endpoint: Catch Alerts Settings from Website
  AsyncCallbackJsonWebHandler* alertHandler = new AsyncCallbackJsonWebHandler("/api/save-alerts", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    
    // Update live variables
    twilio_msg = jsonObj["msg"].as<String>();
    danger_sms_interval = jsonObj["danger_interval"].as<unsigned long>() * 3600000; // Convert hours to ms
    routine_sms_interval = jsonObj["safe_interval"].as<unsigned long>() * 3600000;
    twilio_to = jsonObj["to_phone"].as<String>();
    twilio_sid = jsonObj["sid"].as<String>();
    twilio_token = jsonObj["token"].as<String>();
    twilio_from = jsonObj["from_phone"].as<String>();

    // Save permanently to Flash Memory
    preferences.begin("wqi-app", false);
    preferences.putString("tw_msg", twilio_msg);
    preferences.putULong("tw_danger_int", danger_sms_interval);
    preferences.putULong("tw_safe_int", routine_sms_interval);
    preferences.putString("tw_to", twilio_to);
    preferences.putString("tw_sid", twilio_sid);
    preferences.putString("tw_token", twilio_token);
    preferences.putString("tw_from", twilio_from);
    preferences.end();

    Serial.println("New Twilio Alerts Settings Saved!");
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  server.addHandler(alertHandler);

  // Start the server
  server.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.print("Web UI running on Core: ");
  Serial.println(xPortGetCoreID());

  // Keep Core 0 alive infinitely
  for(;;) {
    vTaskDelay(1000 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// CORE 1: YOUR EXISTING SENSOR LOGIC
// ==========================================
void sensorTaskCode(void * parameter) {
  Serial.print("Sensor Logic running on Core: ");
  Serial.println(xPortGetCoreID());

  // --- Initialization ---
  loadSettings(); // <-- ALWAYS LOAD SETTINGS FIRST!

  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115.");
    while (1);
  }
  ads.setGain(GAIN_ONE);

  phSensor.begin();
  waterTemp.begin();
  tdsSensor.begin();
  
  // Connects to WiFi & Firebase
  database.begin(); 
  alerts.begin();

  // --- Infinite Sensor Loop ---
  for(;;) {
    // 1. Update sensors
    waterTemp.update();
    tdsSensor.update(waterTemp.getTemp()); 
    phSensor.update(waterTemp.getTemp());

    // 2. Update Globals for Core 0 to read safely
    live_temp = waterTemp.getTemp();
    live_tds = tdsSensor.getTDS();
    live_ph = phSensor.getPH();
    live_hardness = tdsSensor.getHardness();
    
    // 3. Check Alerts & Buzzers
    alerts.checkAlerts(live_ph, live_tds, live_hardness); 
    alerts.update(); 

    // 4. Remote Control Polling
    database.pollSettings();

    // 5. Firebase Sync
    database.processAndPush(
      live_temp, tdsSensor.getEC(), live_tds, 
      tdsSensor.getSalinity(), live_hardness, live_ph
    );

    // Yield to FreeRTOS watchdog
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// SYSTEM BOOTSTRAP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Pin the Web Server to Core 0
  xTaskCreatePinnedToCore(webTaskCode, "WebTask", 10000, NULL, 1, &WebTask, 0);

  // Pin the Heavy Processing to Core 1
  xTaskCreatePinnedToCore(sensorTaskCode, "SensorTask", 10000, NULL, 1, &SensorTask, 1);
}

void loop() {
  // Empty! FreeRTOS tasks handle everything now.
  vTaskDelete(NULL); 
}