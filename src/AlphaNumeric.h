/*
 * Portions of this code was derived from DMDExt and PinMAME
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/DmdDevice/AlphaNumeric.cs
 * https://github.com/vpinball/pinmame/blob/master/ext/dmddevice/usbalphanumeric.h
 */

#pragma once

#include <cstdint>

#include "DMDUtil/DMD.h"

namespace DMDUtil
{

class AlphaNumeric
{
 public:
  AlphaNumeric();
  ~AlphaNumeric() {}

  void Render(uint8_t* pFrame, AlphaNumericLayout layout, const uint16_t* const seg_data);
  void Render(uint8_t* pFrame, AlphaNumericLayout layout, const uint16_t* const seg_data, const uint16_t* const seg_data2);

 private:
  void SmoothDigitCorners(uint8_t* pFrame, const int x, const int y);
  void SmoothDigitCorners6Px(uint8_t* pFrame, const int x, const int y);
  void DrawSegment(uint8_t* pFrame, const int x, const int y, const uint8_t type, const uint16_t seg, const uint8_t colour);
  bool GetPixel(uint8_t* pFrame, const int x, const int y) const;
  void DrawPixel(uint8_t* pFrame, const int x, const int y, const uint8_t colour);
  void Clear(uint8_t* pFrame);

  void Render2x16Alpha(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x20Alpha(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Alpha_2x7Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Alpha_2x7Num_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x6Num_2x6Num_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x6Num10_2x6Num10_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Num_2x7Num_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Num_2x7Num_10x1Num(uint8_t* pFrame, const uint16_t* const seg_data, const uint16_t* const extra_seg_data);
  void Render2x7Num_2x7Num_4x1Num_gen7(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Num10_2x7Num10_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render4x7Num10(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render6x4Num_4x1Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render2x7Num_4x1Num_1x16Alpha(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render1x16Alpha_1x16Num_1x7Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render1x7Num_1x16Alpha_1x16Num(uint8_t* pFrame, const uint16_t* const seg_data);
  void Render1x16Alpha_1x16Num_1x7Num_1x4Num(uint8_t* pFrame, const uint16_t* const seg_data);

  static const uint8_t SegSizes[8][16];
  static const uint8_t Segs[8][17][5][2];
};

}  // namespace DMDUtil
