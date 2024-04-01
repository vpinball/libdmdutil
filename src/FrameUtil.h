/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Common/FrameUtil.cs
 */

#pragma once

#include <cstdint>
#include <string>

namespace DMDUtil
{

enum class ColorMatrix
{
  Rgb,
  Rbg
};

class FrameUtil
{
 public:
  static inline int MapAdafruitIndex(int x, int y, int width, int height, int numLogicalRows);
  static void SplitIntoRgbPlanes(const uint16_t* rgb565, int rgb565Size, int width, int numLogicalRows, uint8_t* dest,
                                 ColorMatrix colorMatrix = ColorMatrix::Rgb);
  static inline uint16_t InterpolateRgb565Color(uint16_t color1, uint16_t color2, float ratio);
  static inline uint16_t InterpolatedRgb565Pixel(const uint16_t* src, float srcX, float srcY, int srcWidth,
                                                 int srcHeight);
  static void ResizeRgb565Bilinear(const uint16_t* src, int srcWidth, int srcHeight, uint16_t* dest, int destWidth,
                                   int destHeight);
  static float CalcBrightness(float x);
  static std::string HexDump(const uint8_t* data, size_t size);
  static void ScaleDownIndexed(uint8_t* pDestFrame, const uint8_t destWidth, const uint8_t destHeight,
                               const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight);
  static void ScaleDownPUP(uint8_t* pDestFrame, const uint8_t destWidth, const uint8_t destHeight,
                           const uint8_t* pSrcFrame, const uint8_t srcWidth, const uint8_t srcHeight);
  static void ScaleDown(uint8_t* pDestFrame, const uint8_t destWidth, const uint8_t destHeight,
                        const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight, uint8_t bits);
  static void CenterIndexed(uint8_t* pDestFrame, const uint8_t destWidth, const uint8_t destHeight,
                            const uint8_t* pSrcFrame, const uint8_t srcWidth, const uint8_t srcHeight);
  static void Center(uint8_t* pDestFrame, const uint8_t destWidth, const uint8_t destHeight, const uint8_t* pSrcFrame,
                     const uint8_t srcWidth, const uint8_t srcHeight, uint8_t bits);
};

}  // namespace DMDUtil
