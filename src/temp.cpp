#include "temp.h"
#include "config.h"

TempSensor::TempSensor(uint8_t pin) : oneWire(pin), sensor(&oneWire) {
  lastTempRequest = 0;
  currentTemperature = 25.0;
}

void TempSensor::begin() {
  sensor.begin();
  sensor.setWaitForConversion(false); 
  sensor.requestTemperatures();       
  lastTempRequest = millis();
}

void TempSensor::update() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastTempRequest >= TEMP_INTERVAL) {
    float tempC = sensor.getTempCByIndex(0);
    if (tempC != DEVICE_DISCONNECTED_C) {
      currentTemperature = tempC;
    }
    sensor.requestTemperatures();
    lastTempRequest = currentMillis;
  }
}

float TempSensor::getTemp() {
  return currentTemperature;
}