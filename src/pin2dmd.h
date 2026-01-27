#ifndef PIN2DMD_H
#define PIN2DMD_H

#include <cstdint>

int Pin2dmdInit();
bool Pin2dmdIsConnected();
uint16_t Pin2dmdGetWidth();
uint16_t Pin2dmdGetHeight();
void Pin2dmdRender(uint16_t width, uint16_t height, uint8_t* buffer, int bitDepth);
void Pin2dmdRenderRaw(uint16_t width, uint16_t height, uint8_t* buffer, uint32_t frames);

#endif /* PIN2DMD_H */
