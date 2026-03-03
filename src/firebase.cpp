#include "firebase.h"
#include "config.h" // Gives us access to dynamic variables
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

FirebaseManager::FirebaseManager() {
  lastOutput = 0;
  lastHistoryPush = 0;
  
  lastPollTime = 0;
  currentMode = "constant"; 
  triggerRequested = false;
  stabilizationActive = false;
  stabilizationStartTime = 0;
}

time_t FirebaseManager::getUnixTimestamp() {
  time_t now;
  time(&now);
  return now;
}

void FirebaseManager::syncTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("Waiting for NTP time sync...");
  struct tm timeinfo;
  int retry = 0;
  while (retry < 10) {
    if (getLocalTime(&timeinfo)) {
      Serial.println("Time synchronized successfully!");
      return;
    }
    delay(500);
    retry++;
  }
}

void FirebaseManager::begin() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected!");

  syncTime();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Auth Successful");
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void FirebaseManager::pollSettings() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = currentMillis;
    
    if (Firebase.ready()) {
      if (Firebase.RTDB.getString(&fbdo, "settings/monitoring_control/mode")) {
        currentMode = fbdo.stringData();
      }
      if (Firebase.RTDB.getBool(&fbdo, "settings/monitoring_control/trigger_reading")) {
        triggerRequested = fbdo.boolData();
      }
    }
  }
}

void FirebaseManager::processAndPush(float temp, float ec, float tds, float salinity, float hardness, float ph) {
  unsigned long currentMillis = millis();

  // ============================================
  // MODE 1: CONSTANT MONITORING (24/7)
  // ============================================
  if (currentMode == "constant") {
    
    // 1. Live Dashboard Output (Every 2 seconds)
    if (currentMillis - lastOutput >= OUTPUT_INTERVAL) {
      if (Firebase.ready()) {
        FirebaseJson json;
        json.set("temp", temp);
        json.set("ec", ec);
        json.set("tds", tds);
        json.set("salinity", salinity);
        json.set("hardness", hardness);
        json.set("ph", ph);        
        json.set("turbidity", 0.5); // Dummy
        Firebase.RTDB.setJSON(&fbdo, "sensors/water-quality", &json);
      }
      lastOutput = currentMillis;
    }

    // 2. Historical Alerts Log (Using the new dynamic fb_update_interval)
    if (currentMillis - lastHistoryPush >= fb_update_interval) {
      time_t timestamp = getUnixTimestamp();
      if (Firebase.ready()) {
        FirebaseJson json;
        json.set("timestamp", (int)timestamp);
        json.set("temp", temp);
        json.set("ec", ec);
        json.set("tds", tds);
        json.set("salinity", salinity);
        json.set("hardness", hardness);
        json.set("ph", ph);
        json.set("turbidity", 0.5); 
        Firebase.RTDB.pushJSON(&fbdo, "sensor_history", &json);
      }
      lastHistoryPush = currentMillis;
    }
  } 
  
  // ============================================
  // MODE 2: ONE-TIME MONITORING (Idle & Trigger)
  // ============================================
  else {
    if (triggerRequested && !stabilizationActive) {
      Serial.println("Trigger Received! Taking 10 seconds to stabilize sensors...");
      stabilizationActive = true;
      stabilizationStartTime = currentMillis;
    }

    if (stabilizationActive) {
      if (currentMillis - stabilizationStartTime >= STABILIZATION_TIME) {
        Serial.println("Sensors stabilized! Pushing perfect data to Firebase...");

        if (Firebase.ready()) {
          FirebaseJson liveJson;
          liveJson.set("temp", temp);
          liveJson.set("ec", ec);
          liveJson.set("tds", tds);
          liveJson.set("salinity", salinity);
          liveJson.set("hardness", hardness);
          liveJson.set("ph", ph);
          liveJson.set("turbidity", 0.5);
          Firebase.RTDB.setJSON(&fbdo, "sensors/water-quality", &liveJson);

          time_t timestamp = getUnixTimestamp();
          FirebaseJson histJson;
          histJson.set("timestamp", (int)timestamp);
          histJson.set("temp", temp);
          histJson.set("ec", ec);
          histJson.set("tds", tds);
          histJson.set("salinity", salinity);
          histJson.set("hardness", hardness);
          histJson.set("ph", ph);
          histJson.set("turbidity", 0.5);
          Firebase.RTDB.pushJSON(&fbdo, "sensor_history", &histJson);

          Firebase.RTDB.setBool(&fbdo, "settings/monitoring_control/trigger_reading", false);
        }
        triggerRequested = false;
        stabilizationActive = false;
      }
    }
  }
}