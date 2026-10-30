#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DMDUtil/DMD.h"
#include "FrameUtil.h"

#if defined(DMDUTIL_HAS_USB_DEVICES) && defined(DMDUTIL_ENABLE_PIXELCADE)
#define DMDUTIL_WITH_PIXELCADE 1
#include "devices/PixelcadeDMD.h"
#endif
#if defined(DMDUTIL_HAS_USB_DEVICES) && defined(DMDUTIL_ENABLE_PIN2DMD)
#define DMDUTIL_WITH_PIN2DMD 1
#include "devices/PIN2DMD.h"
#endif
#if defined(DMDUTIL_HAS_USB_DEVICES) && defined(DMDUTIL_ENABLE_PINDMD3)
#define DMDUTIL_WITH_PINDMD3 1
#include "devices/PinDMD3.h"
#endif
#ifdef DMDUTIL_ENABLE_DMDEXTSTREAM
#define DMDUTIL_WITH_DMDEXTSTREAM 1
#include "devices/DMDExtStream.h"
#endif

namespace DMDUtil
{

#define DMDUTIL_MAX_FRAME_PIXELS (256 * 64)
#define DMDUTIL_MAX_RGB24_BYTES (DMDUTIL_MAX_FRAME_PIXELS * 3)

#define DMDUTIL_MAX_SCALE_TEMP_WIDTH 512
#define DMDUTIL_MAX_SCALE_TEMP_HEIGHT 128

inline size_t PaletteBytesForDepth(uint8_t depth)
{
  if (depth > 8)
  {
    return 0;
  }
  return (static_cast<size_t>(1u) << depth) * 3u;
}

inline bool ScaleFrameToTarget(const uint8_t* pSrc, uint16_t width, uint16_t height, uint8_t* pDst,
                               uint16_t targetWidth, uint16_t targetHeight, uint8_t bits, std::vector<uint8_t>& temp)
{
  const size_t bytesPerPixel = bits / 8;

  if (width == targetWidth && height == targetHeight)
  {
    memcpy(pDst, pSrc, (size_t)targetWidth * targetHeight * bytesPerPixel);
    return true;
  }

  if (width == targetWidth && height == 16 && targetHeight >= 32)
  {
    if (bits == 8)
      FrameUtil::Helper::CenterIndexed(pDst, targetWidth, (uint8_t)targetHeight, pSrc, targetWidth, 16);
    else
      FrameUtil::Helper::Center(pDst, targetWidth, (uint8_t)targetHeight, pSrc, targetWidth, 16, bits);
    return true;
  }

  if (height == 64 && targetHeight == 32)
  {
    if (bits == 8)
      FrameUtil::Helper::ScaleDownIndexed(pDst, targetWidth, (uint8_t)targetHeight, pSrc, width, 64);
    else
      FrameUtil::Helper::ScaleDown(pDst, targetWidth, (uint8_t)targetHeight, pSrc, width, 64, bits);
    return true;
  }

  if (bits == 8) return false;

  const uint16_t upWidth = width * 2;
  const uint16_t upHeight = height * 2;
  const bool canUpscale = upWidth <= DMDUTIL_MAX_SCALE_TEMP_WIDTH && upHeight <= DMDUTIL_MAX_SCALE_TEMP_HEIGHT;

  if (height == 32 && targetHeight == 64)
  {
    if (upWidth == targetWidth)
    {
      FrameUtil::Helper::ScaleUp(pDst, pSrc, width, (uint8_t)height, bits);
      return true;
    }
    if (!canUpscale) return false;
    temp.resize((size_t)DMDUTIL_MAX_SCALE_TEMP_WIDTH * DMDUTIL_MAX_SCALE_TEMP_HEIGHT * bytesPerPixel);
    FrameUtil::Helper::ScaleUp(temp.data(), pSrc, width, (uint8_t)height, bits);
    FrameUtil::Helper::ScaleDown(pDst, targetWidth, (uint8_t)targetHeight, temp.data(), upWidth, (uint8_t)upHeight,
                                 bits);
    return true;
  }

  if (height == 64 && targetHeight == 64)
  {
    if (width > targetWidth)
    {
      FrameUtil::Helper::ScaleDown(pDst, targetWidth, (uint8_t)targetHeight, pSrc, width, (uint8_t)height, bits);
      return true;
    }
    if (!canUpscale) return false;
    temp.resize((size_t)DMDUTIL_MAX_SCALE_TEMP_WIDTH * DMDUTIL_MAX_SCALE_TEMP_HEIGHT * bytesPerPixel);
    FrameUtil::Helper::ScaleUp(temp.data(), pSrc, width, (uint8_t)height, bits);
    FrameUtil::Helper::ScaleDown(pDst, targetWidth, (uint8_t)targetHeight, temp.data(), upWidth, (uint8_t)upHeight,
                                 bits);
    return true;
  }

  return false;
}

inline void Rgb565ToRgb888(const uint16_t* __restrict pRgb565, uint8_t* __restrict pRgb888, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    const uint16_t value = pRgb565[i];
    const uint8_t r = (uint8_t)((value >> 11) & 0x1F);
    const uint8_t g = (uint8_t)((value >> 5) & 0x3F);
    const uint8_t b = (uint8_t)(value & 0x1F);
    pRgb888[i * 3] = (uint8_t)((r << 3) | (r >> 2));
    pRgb888[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
    pRgb888[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
  }
}

inline void Rgb888ToRgb565(const uint8_t* __restrict pRgb888, uint16_t* __restrict pRgb565, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    const uint32_t r = pRgb888[i * 3];
    const uint32_t g = pRgb888[i * 3 + 1];
    const uint32_t b = pRgb888[i * 3 + 2];
    pRgb565[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
  }
}

struct DMD::OutputDecoder
{
  bool excludeColorized = false;
  bool showNotColorized = false;
  uint16_t targetWidth = 0;
  Mode mode = Mode::Unknown;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t depth = 0;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  bool rgb565Source = false;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[256 * 3] = {0};
  uint8_t indexed[256 * 64] = {0};
  uint8_t rawRgb24[256 * 64 * 3] = {0};
  uint8_t rgb24[256 * 64 * 3] = {0};
};

}  // namespace DMDUtil
