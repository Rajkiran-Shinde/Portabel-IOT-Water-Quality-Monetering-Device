#pragma once
#include <Arduino.h>
#include "sms.h" // <-- ADDED THIS

// Define our alert states
enum AlertLevel {
    SAFE,
    CAUTION,
    WARNING,
    DANGER
};

class AlertSystem {
public:
    AlertSystem(uint8_t buzzerPin);
    void begin();
    
    // Evaluates the readings and sets the alert level
    void checkAlerts(float ph, float tds, float hardness, float turbidity = 0.0, float wqi = 0.0);
    
    // Needs to be called continuously in loop() to handle beep timing
    void update(); 

private:
    uint8_t _buzzerPin;
    AlertLevel _currentLevel;
    AlertLevel _lastNotifiedLevel; // <-- ADDED: Tracks the last state we texted you about

    // --- STABILIZATION VARIABLES ADDED ---
    AlertLevel _candidateLevel;    // The temporary level we are evaluating
    unsigned long _candidateMillis; // When this temporary level started
    
    // Timing variables for non-blocking beeps and SMS
    unsigned long _previousMillis;
    unsigned long _lastSMSMillis;  // <-- ADDED: Timer for routine texts
    bool _buzzerState;
    
    SMSManager _sms; // <-- ADDED: Our SMS sender
    
    // Helper function to send the text
    void sendSMSUpdate(float ph, float tds, float hardness); 
};