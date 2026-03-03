#pragma once
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h>

class FirebaseManager {
  private:
    FirebaseData fbdo;
    FirebaseAuth auth;
    FirebaseConfig config;
    unsigned long lastOutput;
    unsigned long lastHistoryPush;

    // --- NEW VARIABLES FOR REMOTE CONTROL ---
    unsigned long lastPollTime;
    String currentMode;
    bool triggerRequested;
    bool stabilizationActive;
    unsigned long stabilizationStartTime;

    time_t getUnixTimestamp();
    void syncTime();

  public:
    FirebaseManager();
    void begin();
    void pollSettings(); // <-- NEW FUNCTION
    void processAndPush(float temp, float ec, float tds, float salinity, float hardness, float ph);
};