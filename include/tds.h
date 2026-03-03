#pragma once
#include <Adafruit_ADS1X15.h>

#define NUM_SAMPLES 30

class TDSSensor {
  private:
    Adafruit_ADS1115* ads;
    uint8_t channel;
    int16_t adcBuffer[NUM_SAMPLES];
    uint8_t sampleIndex;
    unsigned long lastAdcRead;
    
    // Calculated values
    float ecValue;
    float tdsValue;
    float salinityValue;
    float hardnessValue;

    float getFilteredAndCompensatedVoltage(float tempC);

  public:
    TDSSensor(Adafruit_ADS1115* adsPtr, uint8_t adcChannel);
    void begin();
    void update(float currentTemp);
    
    float getEC();
    float getTDS();
    float getSalinity();
    float getHardness();
};