#pragma once
#include <OneWire.h>
#include <DallasTemperature.h>

class TempSensor {
  private:
    OneWire oneWire;
    DallasTemperature sensor;
    unsigned long lastTempRequest;
    float currentTemperature;

  public:
    TempSensor(uint8_t pin);
    void begin();
    void update();
    float getTemp();
};