#pragma once
#include <Adafruit_ADS1X15.h>

class PHSensor {
  private:
    Adafruit_ADS1115* ads;
    uint8_t channel;
    
    float smoothedVoltage;
    float currentPH;
    bool firstRun;

    // Pre-calculated baseline slopes
    float m_acid;
    float m_alkaline;

    // Helper functions hidden from main.cpp
    void sortArray(float arr[], int n);
    float getOlympicVoltage();

  public:
    PHSensor(Adafruit_ADS1115* adsPtr, uint8_t adcChannel);
    void begin();
    void update(float currentTemp);
    
    float getPH();
    float getVoltage();
};