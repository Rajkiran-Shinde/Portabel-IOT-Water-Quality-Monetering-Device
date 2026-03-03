#pragma once
#include <Arduino.h>

class SMSManager {
public:
    void sendAlert(String alertLevel, float ph, float tds, float hardness);
};