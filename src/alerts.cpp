#include "alerts.h"
#include "config.h" // Gives us access to the dynamic variables

AlertSystem::AlertSystem(uint8_t buzzerPin) {
    _buzzerPin = buzzerPin;
    _currentLevel = SAFE;
    _lastNotifiedLevel = SAFE; 
    _candidateLevel = SAFE;     
    _candidateMillis = 0;       
    _previousMillis = 0;
    _lastSMSMillis = 0;        
    _buzzerState = false;
}

void AlertSystem::begin() {
    pinMode(_buzzerPin, OUTPUT);
    digitalWrite(_buzzerPin, LOW); 
}

void AlertSystem::checkAlerts(float ph, float tds, float hardness, float turbidity, float wqi) {
    if (millis() < STARTUP_DELAY) {
        return; 
    }

    // 1. Calculate the INSTANTANEOUS level
    AlertLevel instantLevel = SAFE; 

    if (tds > 1600.0) instantLevel = max(instantLevel, DANGER);
    else if (tds > 500.0) instantLevel = max(instantLevel, WARNING);
    else if (tds > 400.0) instantLevel = max(instantLevel, CAUTION);

    if (hardness > 600.0) instantLevel = max(instantLevel, DANGER);
    else if (hardness > 200.0) instantLevel = max(instantLevel, WARNING);
    else if (hardness > 150.0) instantLevel = max(instantLevel, CAUTION);

    if (ph < 6.0 || ph > 9.0) instantLevel = max(instantLevel, DANGER);
    else if (ph < 6.5 || ph > 8.5) instantLevel = max(instantLevel, WARNING);
    else if (ph < 6.8 || ph > 8.2) instantLevel = max(instantLevel, CAUTION);

    // ==========================================
    // --- STABILIZATION LOGIC ---
    // ==========================================
    if (instantLevel != _candidateLevel) {
        _candidateLevel = instantLevel;
        _candidateMillis = millis(); 
    }

    if (millis() - _candidateMillis >= SENSOR_STABILIZE_TIME) {
        _currentLevel = _candidateLevel; 
    }

    // ==========================================
    // --- SMART SMS GATEKEEPER LOGIC ---
    // ==========================================
    unsigned long currentMillis = millis();
    
    // --- USING DYNAMIC VARIABLES HERE ---
    unsigned long currentInterval = (_currentLevel == DANGER) ? danger_sms_interval : routine_sms_interval;

    if (_currentLevel > _lastNotifiedLevel) {
        sendSMSUpdate(ph, tds, hardness);
        _lastNotifiedLevel = _currentLevel; 
        _lastSMSMillis = currentMillis; 
    } 
    else if (currentMillis - _lastSMSMillis >= currentInterval) {
        sendSMSUpdate(ph, tds, hardness);
        _lastSMSMillis = currentMillis; 
    }
    else if (_currentLevel < _lastNotifiedLevel) {
        _lastNotifiedLevel = _currentLevel;
    }
}

void AlertSystem::sendSMSUpdate(float ph, float tds, float hardness) {
    String levelStr = "SAFE (Routine Update)";
    if (_currentLevel == CAUTION) levelStr = "CAUTION (Near Limits)";
    if (_currentLevel == WARNING) levelStr = "WARNING (Exceeds Acceptable)";
    if (_currentLevel == DANGER) levelStr = "DANGER (Beyond Permissible)";
    
    Serial.println("Triggering SMS via Twilio...");
    _sms.sendAlert(levelStr, ph, tds, hardness);
}

void AlertSystem::update() {
    unsigned long currentMillis = millis();

    switch (_currentLevel) {
        case SAFE:
            _buzzerState = false;
            digitalWrite(_buzzerPin, LOW);
            break;

        case DANGER:
            _buzzerState = true;
            digitalWrite(_buzzerPin, HIGH); // Solid tone
            break;

        case WARNING:
            // Fast beep (toggle every 250ms)
            if (currentMillis - _previousMillis >= 250) {
                _previousMillis = currentMillis;
                _buzzerState = !_buzzerState;
                digitalWrite(_buzzerPin, _buzzerState ? HIGH : LOW);
            }
            break;

        case CAUTION:
            // Slow beep (toggle every 1000ms)
            if (currentMillis - _previousMillis >= 1000) {
                _previousMillis = currentMillis;
                _buzzerState = !_buzzerState;
                digitalWrite(_buzzerPin, _buzzerState ? HIGH : LOW);
            }
            break;
    }
}