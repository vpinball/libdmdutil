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

  uint8_t* Render(AlphaNumericLayout layout, const uint16_t* const seg_data);
  uint8_t* Render(AlphaNumericLayout layout, const uint16_t* const seg_data, const uint16_t* const seg_data2);

 private:
  void SmoothDigitCorners(const int x, const int y);
  void SmoothDigitCorners6Px(const int x, const int y);
  void DrawSegment(const int x, const int y, const uint8_t type, const uint16_t seg, const uint8_t colour);
  bool GetPixel(const int x, const int y) const;
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

  static const uint8_t SegSizes[8][16];
  static const uint8_t Segs[8][17][5][2];
};

}  // namespace DMDUtil
