#include "tds.h"
#include "config.h"

TDSSensor::TDSSensor(Adafruit_ADS1115* adsPtr, uint8_t adcChannel) {
  ads = adsPtr;
  channel = adcChannel;
  sampleIndex = 0;
  lastAdcRead = 0;
  ecValue = 0.0;
  tdsValue = 0.0;
  salinityValue = 0.0;
  hardnessValue = 0.0;
}

void TDSSensor::begin() {
  for(int i=0; i<NUM_SAMPLES; i++) {
    adcBuffer[i] = 0;
  }
}

float TDSSensor::getFilteredAndCompensatedVoltage(float tempC) {
  int16_t sortedSamples[NUM_SAMPLES];
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sortedSamples[i] = adcBuffer[i];
  }

  // Bubble Sort
  for (int i = 0; i < NUM_SAMPLES - 1; i++) {
    for (int j = i + 1; j < NUM_SAMPLES; j++) {
      if (sortedSamples[i] > sortedSamples[j]) {
        int16_t temp = sortedSamples[i];
        sortedSamples[i] = sortedSamples[j];
        sortedSamples[j] = temp;
      }
    }
  }

  // Average middle values
  long sum = 0;
  int validSampleCount = 0;
  for (int i = 5; i < NUM_SAMPLES - 5; i++) {
    sum += sortedSamples[i];
    validSampleCount++;
  }
  
  float averageADC = (float)sum / validSampleCount;
  float rawVoltage = averageADC * VOLTS_PER_BIT;
  
  // Temperature Compensation
  float compensationCoefficient = 1.0 + 0.02 * (tempC - 25.0);
  return rawVoltage / compensationCoefficient;
}

void TDSSensor::update(float currentTemp) {
  unsigned long currentMillis = millis();

  // Async Sampling
  if (currentMillis - lastAdcRead >= ADC_INTERVAL) {
    adcBuffer[sampleIndex] = ads->readADC_SingleEnded(channel); 
    sampleIndex++;
    if (sampleIndex >= NUM_SAMPLES) sampleIndex = 0; 
    lastAdcRead = currentMillis;
  }

  // Calculate Values constantly based on latest buffer
  float compensatedVoltage = getFilteredAndCompensatedVoltage(currentTemp);
  ecValue = (73.787* compensatedVoltage * compensatedVoltage) + (608.27 * compensatedVoltage) - 13.535;
  if (ecValue < 0) ecValue = 0;

  tdsValue = ecValue * TDS_FACTOR;               
  salinityValue = ecValue * SALINITY_FACTOR;     
  hardnessValue = tdsValue / HARDNESS_FACTOR;
}

float TDSSensor::getEC() { return ecValue; }
float TDSSensor::getTDS() { return tdsValue; }
float TDSSensor::getSalinity() { return salinityValue; }
float TDSSensor::getHardness() { return hardnessValue; }