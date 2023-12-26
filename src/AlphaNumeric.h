/*
 * Portions of this code was derived from DMDExt and PinMAME
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/DmdDevice/AlphaNumeric.cs
 * https://github.com/vpinball/pinmame/blob/master/ext/dmddevice/usbalphanumeric.h
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace DMDUtil {

typedef enum {
   None,
   __2x16Alpha,
   __2x20Alpha,
   __2x7Alpha_2x7Num,
   __2x7Alpha_2x7Num_4x1Num,
   __2x7Num_2x7Num_4x1Num,
   __2x7Num_2x7Num_10x1Num,
   __2x7Num_2x7Num_4x1Num_gen7,
   __2x7Num10_2x7Num10_4x1Num,
   __2x6Num_2x6Num_4x1Num,
   __2x6Num10_2x6Num10_4x1Num,
   __4x7Num10,
   __6x4Num_4x1Num,
   __2x7Num_4x1Num_1x16Alpha,
   __1x16Alpha_1x16Num_1x7Num,
   __1x7Num_1x16Alpha_1x16Num,
   __1x16Alpha_1x16Num_1x7Num_1x4Num
} NumericalLayout;

class AlphaNumeric
{
public:
   AlphaNumeric();
   ~AlphaNumeric() {};

   uint8_t* Render(NumericalLayout layout, const uint16_t* const seg_data);
   uint8_t* Render(NumericalLayout layout, const uint16_t* const seg_data, const uint16_t* const seg_data2);

private:
   void SmoothDigitCorners(const int x, const int y);
   void SmoothDigitCorners6Px(const int x, const int y);
   void DrawSegment(const int x, const int y, const uint8_t type, const uint16_t seg, const uint8_t colour);
   uint8_t GetPixel(const int x, const int y);
   void DrawPixel(const int x, const int y, const uint8_t colour);
   void Clear();

   void Render2x16Alpha(const uint16_t* const seg_data);
   void Render2x20Alpha(const uint16_t* const seg_data);
   void Render2x7Alpha_2x7Num(const uint16_t* const seg_data);
   void Render2x7Alpha_2x7Num_4x1Num(const uint16_t* const seg_data);
   void Render2x6Num_2x6Num_4x1Num(const uint16_t* const seg_data);
   void Render2x6Num10_2x6Num10_4x1Num(const uint16_t* const seg_data);
   void Render2x7Num_2x7Num_4x1Num(const uint16_t* const seg_data);
   void Render2x7Num_2x7Num_10x1Num(const uint16_t* const seg_data, const uint16_t* const extra_seg_data);
   void Render2x7Num_2x7Num_4x1Num_gen7(const uint16_t* const seg_data);
   void Render2x7Num10_2x7Num10_4x1Num(const uint16_t* const seg_data);
   void Render4x7Num10(const uint16_t* const seg_data);
   void Render6x4Num_4x1Num(const uint16_t* const seg_data);
   void Render2x7Num_4x1Num_1x16Alpha(const uint16_t* const seg_data);
   void Render1x16Alpha_1x16Num_1x7Num(const uint16_t* const seg_data);
   void Render1x7Num_1x16Alpha_1x16Num(const uint16_t* const seg_data);
   void Render1x16Alpha_1x16Num_1x7Num_1x4Num(const uint16_t* const seg_data);

   uint8_t m_frameBuffer[4096];

   static uint8_t SegSizes[8][16];
   static uint8_t Segs[8][17][5][2];
};

}