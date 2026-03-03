#include "ph.h"
#include "config.h"

// Constructor
PHSensor::PHSensor(Adafruit_ADS1115* adsPtr, uint8_t adcChannel) {
  ads = adsPtr;
  channel = adcChannel;
  smoothedVoltage = 0.0;
  currentPH = 7.0;
  firstRun = true;

  // Pre-calculate the baseline slopes exactly once to save CPU cycles
  m_acid = (6.86 - 4.00) / (CAL_V_686 - CAL_V_400); 
  m_alkaline = (9.18 - 6.86) / (CAL_V_918 - CAL_V_686);
}

void PHSensor::begin() {
  const float CAL_V_686 = 2.582; // Voltage at pH 6.86
const float CAL_V_400 = 2.857; // Voltage at pH 4.00
const float CAL_V_918 = 1.999; // Voltage at pH 9.18
const float CAL_TEMP_C = 31.3; // The physical temperature during calibration
  // Any specific setup for pH can go here. 
  // The ADS1115 is already started in main.cpp.
}

// Simple Bubble Sort
void PHSensor::sortArray(float arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        float temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

// Takes a burst of readings and applies Olympic scoring
float PHSensor::getOlympicVoltage() {
  float voltageArray[PH_NUM_SAMPLES];

  for (int i = 0; i < PH_NUM_SAMPLES; i++) {
    int16_t adcRaw = ads->readADC_SingleEnded(channel);
    voltageArray[i] = ads->computeVolts(adcRaw); 
    delay(5); // Settle time
  }

  sortArray(voltageArray, PH_NUM_SAMPLES);

  float total = 0;
  int count = 0;
  
  // For 20 samples: discard bottom 4 and top 4, average the middle 12
  for (int i = 4; i < 16; i++) {
    total += voltageArray[i];
    count++;
  }

  return total / count;
}

void PHSensor::update(float currentTemp) {
  // 1. Get the raw Olympic Scored Voltage
  float olympicVoltage = getOlympicVoltage();

  // 2. Apply Exponential Moving Average (EMA) Smoothing
  if (firstRun) {
    smoothedVoltage = olympicVoltage; 
    firstRun = false;
  } else {
    smoothedVoltage = (PH_EMA_ALPHA * olympicVoltage) + ((1.0 - PH_EMA_ALPHA) * smoothedVoltage);
  }

  // 3. Calculate pH with 3-Point Split
  float base_slope = 0.0;
  if (smoothedVoltage > CAL_V_686) {
    base_slope = m_acid;
  } else {
    base_slope = m_alkaline;
  }

  // 4. Apply Temperature Compensation (Kelvin scaling)
  float current_slope = base_slope * ((currentTemp + 273.15) / (CAL_TEMP_C + 273.15));

  // 5. Final pH Calculation
  currentPH = 6.86 + (current_slope * (smoothedVoltage - CAL_V_686));
}

float PHSensor::getPH() { return currentPH; }
float PHSensor::getVoltage() { return smoothedVoltage; }