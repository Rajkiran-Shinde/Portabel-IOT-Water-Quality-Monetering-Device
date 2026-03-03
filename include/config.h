#pragma once
#include <Arduino.h>

// --- WiFi & Firebase Credentials ---
#define WIFI_SSID "NODEIOT"
#define WIFI_PASSWORD "MCUESP8266"
#define API_KEY "AIzaSyAH_qoGe9siWiQ_6OJYvRXWF_T-8Jg2P2U"
#define DATABASE_URL "https://wqiv1-7588d-default-rtdb.asia-southeast1.firebasedatabase.app"

/*// --- Twilio SMS Credentials ---
#define TWILIO_ACCOUNT_SID "ACe85a8b4f91d6e7acece3f3efef6d8cf7" // Paste your SID here
#define TWILIO_AUTH_TOKEN "e720e1e1ea7c314072948491fad905fd"  // Paste your Auth Token here
#define TWILIO_FROM_NUM "+13188273764"                         // Paste your Twilio Number
#define TWILIO_TO_NUM "+919767975375"                         // Paste your verified personal number
*/

extern String twilio_sid;
extern String twilio_token;
extern String twilio_from;
extern String twilio_to;
extern String twilio_msg;

/*
// --- SMS Timing Intervals (in milliseconds) ---
const unsigned long ROUTINE_SMS_INTERVAL = 14400000; // 4 hours (Routine update)
const unsigned long DANGER_SMS_INTERVAL = 3600000;   // 1 hour (Reminder if still in danger)
*/

extern unsigned long routine_sms_interval;
extern unsigned long danger_sms_interval;


// --- Hardware Pins & Settings ---
#define ONE_WIRE_BUS 4
#define TDS_ADC_CHANNEL 0
#define BUZZER_PIN 18 // <-- ADD THIS: GPIO pin for your Buzzer
const float VOLTS_PER_BIT = 0.000125; // Multiplier for ADS1115 at GAIN_ONE

// --- Timing Intervals (in milliseconds) ---
const int TEMP_INTERVAL = 750;
const int ADC_INTERVAL = 10;
const int OUTPUT_INTERVAL = 2000;
//const unsigned long HISTORY_INTERVAL = 300000; // 5 minutes
extern unsigned long fb_update_interval;
const unsigned long STARTUP_DELAY = 15000;
const unsigned long SENSOR_STABILIZE_TIME = 8000; // <-- ADD THIS: Wait 8 seconds for readings to settle after a dip
const unsigned long POLL_INTERVAL = 3000; // <-- NEW: Check Firebase remote control every 3s
const unsigned long STABILIZATION_TIME = 10000; // <-- NEW: 10 seconds for sensors to settle


// --- Math Factors ---
const float TDS_FACTOR = 0.5;
const float SALINITY_FACTOR = 0.0005;
const float HARDNESS_FACTOR = 17.848;

// --- NTP Time Configuration ---
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 19800
#define DAYLIGHT_OFFSET_SEC 0


//Ph sensor Settings. 
#define PH_ADC_CHANNEL 1          // Assuming A1 (TDS is on A0)
const int PH_NUM_SAMPLES = 20;
const float PH_EMA_ALPHA = 0.2;

// From your Excel Data
const float CAL_V_686 = 2.582; 
const float CAL_V_400 = 2.857; 
const float CAL_V_918 = 1.999; 
const float CAL_TEMP_C = 31.3;