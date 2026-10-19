#pragma once
#include "Arduino.h"
class Preferences { public:
  bool begin(const char*,bool){return true;} void end(){}
  uint8_t getUChar(const char*,uint8_t d){return d;} size_t putUChar(const char*,uint8_t){return 1;}
  uint16_t getUShort(const char*,uint16_t d){return d;} size_t putUShort(const char*,uint16_t){return 2;}
  int32_t getInt(const char*,int32_t d){return d;} size_t putInt(const char*,int32_t){return 4;}
  bool getBool(const char*,bool d){return d;} size_t putBool(const char*,bool){return 1;}
  size_t getBytesLength(const char*){return 0;} size_t getBytes(const char*,void*,size_t){return 0;} size_t putBytes(const char*,const void*,size_t n){return n;}
};
