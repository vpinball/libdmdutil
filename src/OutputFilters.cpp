#include "OutputFilters.h"

#include <algorithm>
#include <cstring>

namespace DMDUtil
{

namespace
{
int ClampCornerRadius(uint16_t width, uint16_t height, int radius)
{
  if (radius <= 0 || width == 0 || height == 0)
  {
    return 0;
  }

  return std::min<int>(radius, std::min<int>(width, height) / 2);
}
}  // namespace

void ApplyRoundedCornersRGB24(uint8_t* pData, uint16_t width, uint16_t height, int radius)
{
  if (pData == nullptr)
  {
    return;
  }

  const int maxRadius = ClampCornerRadius(width, height, radius);
  if (maxRadius <= 0)
  {
    return;
  }

  const float circleRadius = static_cast<float>(maxRadius);
  for (int y = 0; y < maxRadius; ++y)
  {
    for (int x = 0; x < maxRadius; ++x)
    {
      const float dx = circleRadius - (static_cast<float>(x) + 0.5f);
      const float dy = circleRadius - (static_cast<float>(y) + 0.5f);
      if (dx * dx + dy * dy <= circleRadius * circleRadius)
      {
        continue;
      }

      const size_t topLeft = ((size_t)y * width + x) * 3u;
      const size_t topRight = ((size_t)y * width + (width - 1 - x)) * 3u;
      const size_t bottomLeft = (((size_t)height - 1 - y) * width + x) * 3u;
      const size_t bottomRight = (((size_t)height - 1 - y) * width + (width - 1 - x)) * 3u;

      memset(pData + topLeft, 0, 3);
      memset(pData + topRight, 0, 3);
      memset(pData + bottomLeft, 0, 3);
      memset(pData + bottomRight, 0, 3);
    }
  }
}

void ApplyRoundedCornersRGB565(uint16_t* pData, uint16_t width, uint16_t height, int radius)
{
  if (pData == nullptr)
  {
    return;
  }

  const int maxRadius = ClampCornerRadius(width, height, radius);
  if (maxRadius <= 0)
  {
    return;
  }

  const float circleRadius = static_cast<float>(maxRadius);
  for (int y = 0; y < maxRadius; ++y)
  {
    for (int x = 0; x < maxRadius; ++x)
    {
      const float dx = circleRadius - (static_cast<float>(x) + 0.5f);
      const float dy = circleRadius - (static_cast<float>(y) + 0.5f);
      if (dx * dx + dy * dy <= circleRadius * circleRadius)
      {
        continue;
      }

      pData[(size_t)y * width + x] = 0;
      pData[(size_t)y * width + (width - 1 - x)] = 0;
      pData[((size_t)height - 1 - y) * width + x] = 0;
      pData[((size_t)height - 1 - y) * width + (width - 1 - x)] = 0;
    }
  }
}

}  // namespace DMDUtil
