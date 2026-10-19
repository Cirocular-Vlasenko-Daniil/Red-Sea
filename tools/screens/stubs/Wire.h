#pragma once
#include "Arduino.h"
#define I2C_BUFFER_LENGTH 128
class TwoWire { public: void begin(int,int){} void setClock(uint32_t){} void beginTransmission(uint8_t){} size_t write(uint8_t){return 1;} uint8_t endTransmission(){return 0;} };
extern TwoWire Wire;
