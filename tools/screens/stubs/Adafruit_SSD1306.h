#pragma once
#include <Adafruit_GFX.h>
#include <Wire.h>
#define SSD1306_BLACK 0
#define SSD1306_WHITE 1
#define SSD1306_INVERSE 2
#define SSD1306_SWITCHCAPVCC 2
#define SSD1306_PAGEADDR 0x22
#define SSD1306_COLUMNADDR 0x21
class Adafruit_SSD1306 : public Adafruit_GFX {
public:
  Adafruit_SSD1306(uint8_t w,uint8_t h,TwoWire*tw,int8_t):Adafruit_GFX(w,h),wire(tw){buffer=(uint8_t*)calloc(w*((h+7)/8),1);}
  bool begin(uint8_t,uint8_t){return true;}
  void display(){}
  void clearDisplay(){memset(buffer,0,WIDTH*((HEIGHT+7)/8));}
  void drawPixel(int16_t x,int16_t y,uint16_t c) override {
    if(x<0||y<0||x>=WIDTH||y>=HEIGHT)return;
    uint8_t&b=buffer[x+(y/8)*WIDTH]; uint8_t m=1<<(y&7);
    if(c==SSD1306_WHITE)b|=m; else if(c==SSD1306_BLACK)b&=~m; else b^=m;
  }
  uint8_t* getBuffer(){return buffer;}
  bool getPixel(int16_t x,int16_t y){return buffer[x+(y/8)*WIDTH]&(1<<(y&7));}
protected:
  void ssd1306_commandList(const uint8_t*,uint8_t){}
  void ssd1306_command1(uint8_t){}
  TwoWire* wire; uint32_t wireClk=0, restoreClk=0; uint8_t i2caddr=0x3C;
  uint8_t* buffer;
};
