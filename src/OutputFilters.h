#pragma once

#include <cstdint>

namespace DMDUtil
{

void ApplyRoundedCornersRGB24(uint8_t* pData, uint16_t width, uint16_t height, int radius);
void ApplyRoundedCornersRGB565(uint16_t* pData, uint16_t width, uint16_t height, int radius);

}  // namespace DMDUtil
