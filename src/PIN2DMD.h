#ifndef PIN2DMD_H
#define PIN2DMD_H

#include <cstdint>

int PIN2DMDInit();
bool PIN2DMDIsConnected();
uint16_t PIN2DMDGetWidth();
uint16_t PIN2DMDGetHeight();
void PIN2DMDRender(uint16_t width, uint16_t height, uint8_t* buffer, int bitDepth);
void PIN2DMDRenderRaw(uint16_t width, uint16_t height, uint8_t* buffer, uint32_t frames);

#endif /* PIN2DMD_H */
