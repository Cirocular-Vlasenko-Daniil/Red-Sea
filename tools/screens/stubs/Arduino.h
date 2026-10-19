#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#ifndef ARDUINO
#define ARDUINO 10800
#endif
class __FlashStringHelper;
class String { public: const char* c_str() const { return ""; } unsigned length() const { return 0; } };
#define PROGMEM
#define IRAM_ATTR
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
typedef bool boolean;
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define CHANGE 1
#define SERIAL_8N1 0
using std::min; using std::max;
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define radians(d) ((d)*0.017453292519943295)
#define degrees(r) ((r)*57.29577951308232)
inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
extern uint32_t g_ms, g_us;
inline uint32_t millis(){return g_ms;}
inline uint32_t micros(){return g_us;}
inline void delay(uint32_t){}
inline void delayMicroseconds(uint32_t){}
extern uint32_t g_rnd;
inline uint32_t rnd32(){g_rnd=g_rnd*1664525u+1013904223u;return g_rnd>>1;}
inline long random(long hi){return hi<=0?0:(long)(rnd32()%(uint32_t)hi);}
inline long random(long lo,long hi){return hi<=lo?lo:lo+(long)(rnd32()%(uint32_t)(hi-lo));}
inline void randomSeed(unsigned long){}
extern int g_pins[64];
inline int digitalRead(uint8_t p){return g_pins[p];}
inline void pinMode(uint8_t,uint8_t){}
inline int analogRead(uint8_t){return 0;}
inline void attachInterrupt(int,void(*)(),int){}
inline int digitalPinToInterrupt(int p){return p;}
inline void noInterrupts(){}
inline void interrupts(){}
class Print {
public:
  virtual ~Print(){}
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t*b,size_t n){size_t r=0;while(n--)r+=write(*b++);return r;}
  size_t print(const char*s){return write((const uint8_t*)s,strlen(s));}
  size_t print(char c){return write((uint8_t)c);}
  size_t print(int v){char b[24];snprintf(b,24,"%d",v);return print(b);}
  size_t print(unsigned v){char b[24];snprintf(b,24,"%u",v);return print(b);}
  size_t print(long v){char b[24];snprintf(b,24,"%ld",v);return print(b);}
  size_t print(unsigned long v){char b[24];snprintf(b,24,"%lu",v);return print(b);}
  size_t print(double v){char b[24];snprintf(b,24,"%.2f",v);return print(b);}
};
class HardwareSerial { public: HardwareSerial(int){} void begin(uint32_t,int,int,int){} int available(){return 0;} int read(){return -1;} };
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFF
#define configMAX_PRIORITIES 25
#define pdMS_TO_TICKS(x) (x)
inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,TaskHandle_t*h){if(h)*h=nullptr;return 1;}
inline void xTaskNotifyGive(TaskHandle_t){}
inline uint32_t ulTaskNotifyTake(int,uint32_t){return 0;}
inline void vTaskDelay(uint32_t){}
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){return nullptr;}
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t,uint32_t){return 1;}
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t){return 1;}
