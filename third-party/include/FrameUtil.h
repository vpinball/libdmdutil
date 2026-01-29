/*
 * Portions of this code was derived from DMDExt:
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Common/FrameUtil.cs
 *
 * Some were extracted from libzedmd:
 * https://github.com/PPUC/libzedmd
 */

#pragma once

#define FRAMEUTIL_VERSION_MAJOR 0  // X Digits
#define FRAMEUTIL_VERSION_MINOR 2  // Max 2 Digits
#define FRAMEUTIL_VERSION_PATCH 0  // Max 2 Digits

#define _FRAMEUTIL_STR(x) #x
#define FRAMEUTIL_STR(x) _FRAMEUTIL_STR(x)

#define FRAMEUTIL_VERSION                \
  FRAMEUTIL_STR(FRAMEUTIL_VERSION_MAJOR) \
  "." FRAMEUTIL_STR(FRAMEUTIL_VERSION_MINOR) "." FRAMEUTIL_STR(FRAMEUTIL_VERSION_PATCH)
#define FRAMEUTIL_MINOR_VERSION FRAMEUTIL_STR(FRAMEUTIL_VERSION_MAJOR) "." FRAMEUTIL_STR(FRAMEUTIL_VERSION_MINOR)

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace FrameUtil
{

inline constexpr uint32_t kChecksumTable[256] = {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
  0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
  0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
  0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
  0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
  0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
  0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
  0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
  0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
  0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
  0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
  0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
  0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
  0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
  0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
  0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
  0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
  0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
  0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
  0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
  0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
  0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
  0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
  0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
  0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
  0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
  0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
  0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
  0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
  0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
  0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
  0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
  0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
  0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
  0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
  0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
  0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

inline constexpr uint8_t kReverseByte[256] = {
  0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
  0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
  0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
  0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
  0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
  0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
  0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
  0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
  0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
  0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
  0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
  0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
  0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
  0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
  0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
  0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
  0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
  0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
  0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
  0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
  0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
  0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
  0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
  0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
  0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
  0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
  0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
  0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
  0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
  0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
  0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
  0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};


enum class ColorMatrix
{
  Rgb,
  Rbg
};

class Helper
{
 public:
  static int MapAdafruitIndex(int x, int y, int width, int height, int numLogicalRows);
  static void ConvertToRgb24(uint8_t* pFrameRgb24, uint8_t* pFrame, int size, uint8_t* pPalette);
  static void Split(uint8_t* pPlanes, uint16_t width, uint16_t height, uint8_t bitlen, uint8_t* pFrame);
  static void SplitIntoRgbPlanes(const uint16_t* rgb565, int rgb565Size, int width, int numLogicalRows, uint8_t* dest,
                                 ColorMatrix colorMatrix = ColorMatrix::Rgb);
  static float CalcBrightness(float x);
  static void ScaleDownIndexed(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                               const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight);
  static void ScaleDownPUP(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                           const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight);
  static void ScaleDown(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                        const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight, uint8_t bits);
  static void ScaleUpIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, const uint16_t srcWidth,
                             const uint8_t srcHeight);
  static void ScaleUp(uint8_t* pDestFrame, const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight,
                      uint8_t bits);
  static void CenterIndexed(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                            const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight);
  static void Join(uint8_t* pFrame, uint16_t width, uint16_t height, uint8_t bitlen, const uint8_t* pPlanes);
  static std::vector<uint8_t> NewPlane(uint16_t width, uint16_t height);
  static void ClearPlane(uint8_t* plane, size_t len);
  static void OrPlane(const uint8_t* plane, uint8_t* target, size_t len);
  static void CombinePlaneWithMask(const uint8_t* planeA, const uint8_t* planeB, const uint8_t* mask, uint8_t* out,
                                   size_t len);
  static uint8_t ReverseByte(uint8_t value);
  static uint32_t Checksum(const uint8_t* input, size_t len, bool reverse = false);
  static uint32_t ChecksumWithMask(const uint8_t* input, const uint8_t* mask, size_t len, bool reverse = false);
  static void Scale2XIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, uint16_t srcWidth, uint16_t srcHeight);
  static void ScaleDoubleIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, uint16_t srcWidth, uint16_t srcHeight);

  static void Center(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight, const uint8_t* pSrcFrame,
                     const uint16_t srcWidth, const uint8_t srcHeight, uint8_t bits);
};

inline int Helper::MapAdafruitIndex(int x, int y, int width, int height, int numLogicalRows)
{
  int logicalRowLengthPerMatrix = 32 * 32 / 2 / numLogicalRows;
  int logicalRow = y % numLogicalRows;
  int dotPairsPerLogicalRow = width * height / numLogicalRows / 2;
  int widthInMatrices = width / 32;
  int matrixX = x / 32;
  int matrixY = y / 32;
  int totalMatrices = width * height / 1024;
  int matrixNumber = totalMatrices - ((matrixY + 1) * widthInMatrices) + matrixX;
  int indexWithinMatrixRow = x % logicalRowLengthPerMatrix;
  int index = logicalRow * dotPairsPerLogicalRow + matrixNumber * logicalRowLengthPerMatrix + indexWithinMatrixRow;
  return index;
}

inline void Helper::Split(uint8_t* pPlanes, uint16_t width, uint16_t height, uint8_t bitlen, uint8_t* pFrame)
{
  int planeSize = width * height / 8;
  int pos = 0;
  uint8_t* bd = (uint8_t*)malloc(bitlen);

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x += 8)
    {
      memset(bd, 0, bitlen * sizeof(uint8_t));

      for (int v = 7; v >= 0; v--)
      {
        uint8_t pixel = pFrame[(y * width) + (x + v)];
        for (int i = 0; i < bitlen; i++)
        {
          bd[i] <<= 1;
          if ((pixel & (1 << i)) != 0)
          {
            bd[i] |= 1;
          }
        }
      }

      for (int i = 0; i < bitlen; i++)
      {
        pPlanes[i * planeSize + pos] = bd[i];
      }

      pos++;
    }
  }

  free(bd);
}

inline void Helper::ConvertToRgb24(uint8_t* pFrameRgb24, uint8_t* pFrame, int size, uint8_t* pPalette)
{
  for (int i = 0; i < size; i++)
  {
    memcpy(&pFrameRgb24[i * 3], &pPalette[pFrame[i] * 3], 3);
  }
}

inline void Helper::SplitIntoRgbPlanes(const uint16_t* rgb565, int rgb565Size, int width, int numLogicalRows,
                                       uint8_t* dest, ColorMatrix colorMatrix)
{
  constexpr int pairOffset = 16;
  int height = rgb565Size / width;
  int subframeSize = rgb565Size / 2;

  for (int x = 0; x < width; ++x)
  {
    for (int y = 0; y < height; ++y)
    {
      if (y % (pairOffset * 2) >= pairOffset) continue;

      int inputIndex0 = y * width + x;
      int inputIndex1 = inputIndex0 + pairOffset * width;

      uint16_t color0 = rgb565[inputIndex0];
      uint16_t color1 = rgb565[inputIndex1];

      int r0 = 0, r1 = 0, g0 = 0, g1 = 0, b0 = 0, b1 = 0;
      switch (colorMatrix)
      {
        case ColorMatrix::Rgb:
          r0 = (color0 >> 13) /*& 0x7*/;
          g0 = (color0 >> 8) /*& 0x7*/;
          b0 = (color0 >> 2) /*& 0x7*/;
          r1 = (color1 >> 13) /*& 0x7*/;
          g1 = (color1 >> 8) /*& 0x7*/;
          b1 = (color1 >> 2) /*& 0x7*/;
          break;

        case ColorMatrix::Rbg:
          r0 = (color0 >> 13) /*& 0x7*/;
          b0 = (color0 >> 8) /*& 0x7*/;
          g0 = (color0 >> 2) /*& 0x7*/;
          r1 = (color1 >> 13) /*& 0x7*/;
          b1 = (color1 >> 8) /*& 0x7*/;
          g1 = (color1 >> 2) /*& 0x7*/;
          break;
      }

      for (int subframe = 0; subframe < 3; ++subframe)
      {
        uint8_t dotPair = (r0 & 1) << 5 | (g0 & 1) << 4 | (b0 & 1) << 3 | (r1 & 1) << 2 | (g1 & 1) << 1 | (b1 & 1);
        int indexWithinSubframe = MapAdafruitIndex(x, y, width, height, numLogicalRows);
        int indexWithinOutput = subframe * subframeSize + indexWithinSubframe;
        dest[indexWithinOutput] = dotPair;
        r0 >>= 1;
        g0 >>= 1;
        b0 >>= 1;
        r1 >>= 1;
        g1 >>= 1;
        b1 >>= 1;
      }
    }
  }
}

inline float Helper::CalcBrightness(float x)
{
  // function to improve the brightness with fx=ax²+bc+c, f(0)=0, f(1)=1, f'(1.1)=0
  return (-x * x + 2.1f * x) / 1.1f;
}

inline void Helper::ScaleDownIndexed(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                                     const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight)
{
  memset(pDestFrame, 0, destWidth * destHeight);
  uint8_t xOffset = (destWidth - (srcWidth / 2)) / 2;
  uint8_t yOffset = (destHeight - (srcHeight / 2)) / 2;

  // for half scaling we take the 4 points and look if there is one color repeated
  for (uint8_t y = 0; y < srcHeight; y += 2)
  {
    std::vector<uint8_t> row;
    row.reserve(srcWidth / 2);

    for (uint16_t x = 0; x < srcWidth; x += 2)
    {
      uint8_t upper_left = pSrcFrame[y * srcWidth + x];
      uint8_t upper_right = pSrcFrame[y * srcWidth + x + 1];
      uint8_t lower_left = pSrcFrame[(y + 1) * srcWidth + x];
      uint8_t lower_right = pSrcFrame[(y + 1) * srcWidth + x + 1];

      if (x < srcWidth / 2)
      {
        if (y < srcHeight / 2)
        {
          if (upper_left == upper_right || upper_left == lower_left || upper_left == lower_right)
            row.push_back(upper_left);
          else if (upper_right == lower_left || upper_right == lower_right)
            row.push_back(upper_right);
          else if (lower_left == lower_right)
            row.push_back(lower_left);
          else
            row.push_back(upper_left);
        }
        else
        {
          if (lower_left == lower_right || lower_left == upper_left || lower_left == upper_right)
            row.push_back(lower_right);
          else if (lower_right == upper_left || lower_right == upper_right)
            row.push_back(lower_right);
          else if (upper_left == upper_right)
            row.push_back(upper_left);
          else
            row.push_back(lower_left);
        }
      }
      else
      {
        if (y < srcHeight / 2)
        {
          if (upper_right == upper_left || upper_right == lower_right || upper_right == lower_left)
            row.push_back(upper_right);
          else if (upper_left == lower_right || upper_left == lower_left)
            row.push_back(upper_left);
          else if (lower_right == lower_left)
            row.push_back(lower_right);
          else
            row.push_back(upper_right);
        }
        else
        {
          if (lower_right == lower_left || lower_right == upper_right || lower_right == upper_left)
            row.push_back(lower_right);
          else if (lower_left == upper_right || lower_left == upper_left)
            row.push_back(lower_left);
          else if (upper_right == upper_left)
            row.push_back(upper_right);
          else
            row.push_back(lower_right);
        }
      }
    }

    memcpy(&pDestFrame[(yOffset + (y / 2)) * destWidth + xOffset], row.data(), srcWidth / 2);
  }
}

inline void Helper::ScaleDownPUP(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                                 const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight)
{
  memset(pDestFrame, 0, destWidth * destHeight);
  uint8_t xOffset = (destWidth - (srcWidth / 2)) / 2;
  uint8_t yOffset = (destHeight - (srcHeight / 2)) / 2;

  // for half scaling we take the 4 points and look if there is one color repeated
  for (uint8_t y = 0; y < srcHeight; y += 2)
  {
    std::vector<uint8_t> row;
    row.reserve(srcWidth / 2);

    for (uint16_t x = 0; x < srcWidth; x += 2)
    {
      uint8_t pixel1 = pSrcFrame[y * srcWidth + x];
      uint8_t pixel2 = pSrcFrame[y * srcWidth + x + 1];
      uint8_t pixel3 = pSrcFrame[(y + 1) * srcWidth + x];
      uint8_t pixel4 = pSrcFrame[(y + 1) * srcWidth + x + 1];

      if (pixel1 == pixel2 || pixel1 == pixel3 || pixel1 == pixel4)
        row.push_back(pixel1);
      else if (pixel2 == pixel3 || pixel2 == pixel4)
        row.push_back(pixel2);
      else if (pixel3 == pixel4)
        row.push_back(pixel3);
      else
        row.push_back(pixel1);
    }

    memcpy(&pDestFrame[(yOffset + (y / 2)) * destWidth + xOffset], row.data(), srcWidth / 2);
  }
}

inline void Helper::ScaleDown(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                              const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight, uint8_t bits)
{
  uint8_t xOffset = (destWidth - (srcWidth / 2)) / 2;
  uint8_t yOffset = (destHeight - (srcHeight / 2)) / 2;
  uint8_t bytes = bits / 8;  // RGB24 (3 byte) or RGB16 (2 byte) or indexed (1 byte)
  memset(pDestFrame, 0, destWidth * destHeight * bytes);

  for (uint8_t y = 0; y < srcHeight; y += 2)
  {
    uint16_t upper_left = (y * srcWidth) * bytes;
    uint16_t upper_right = upper_left + bytes;
    uint16_t lower_left = upper_left + (srcWidth * bytes);
    uint16_t lower_right = lower_left + bytes;
    uint16_t target = ((((y / 2) + yOffset) * destWidth) + xOffset) * bytes;

    for (uint16_t x = 0; x < srcWidth; x += 2)
    {
      if (x < srcWidth / 2)
      {
        if (y < srcHeight / 2)
        {
          if (memcmp(&pSrcFrame[upper_left], &pSrcFrame[upper_right], bytes) == 0 ||
              memcmp(&pSrcFrame[upper_left], &pSrcFrame[lower_left], bytes) == 0 ||
              memcmp(&pSrcFrame[upper_left], &pSrcFrame[lower_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_left], bytes);
          else if (memcmp(&pSrcFrame[upper_right], &pSrcFrame[lower_left], bytes) == 0 ||
                   memcmp(&pSrcFrame[upper_right], &pSrcFrame[lower_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_right], bytes);
          else if (memcmp(&pSrcFrame[lower_left], &pSrcFrame[lower_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_left], bytes);
          else
            memcpy(&pDestFrame[target], &pSrcFrame[upper_left], bytes);
        }
        else
        {
          if (memcmp(&pSrcFrame[lower_left], &pSrcFrame[lower_right], bytes) == 0 ||
              memcmp(&pSrcFrame[lower_left], &pSrcFrame[upper_left], bytes) == 0 ||
              memcmp(&pSrcFrame[lower_left], &pSrcFrame[upper_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_left], bytes);
          else if (memcmp(&pSrcFrame[lower_right], &pSrcFrame[upper_left], bytes) == 0 ||
                   memcmp(&pSrcFrame[lower_right], &pSrcFrame[upper_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_right], bytes);
          else if (memcmp(&pSrcFrame[upper_left], &pSrcFrame[upper_right], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_left], bytes);
          else
            memcpy(&pDestFrame[target], &pSrcFrame[lower_left], bytes);
        }
      }
      else
      {
        if (y < srcHeight / 2)
        {
          if (memcmp(&pSrcFrame[upper_right], &pSrcFrame[upper_left], bytes) == 0 ||
              memcmp(&pSrcFrame[upper_right], &pSrcFrame[lower_right], bytes) == 0 ||
              memcmp(&pSrcFrame[upper_right], &pSrcFrame[lower_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_right], bytes);
          else if (memcmp(&pSrcFrame[upper_left], &pSrcFrame[lower_right], bytes) == 0 ||
                   memcmp(&pSrcFrame[upper_left], &pSrcFrame[lower_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_left], bytes);
          else if (memcmp(&pSrcFrame[lower_right], &pSrcFrame[lower_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_right], bytes);
          else
            memcpy(&pDestFrame[target], &pSrcFrame[upper_right], bytes);
        }
        else
        {
          if (memcmp(&pSrcFrame[lower_right], &pSrcFrame[lower_left], bytes) == 0 ||
              memcmp(&pSrcFrame[lower_right], &pSrcFrame[upper_right], bytes) == 0 ||
              memcmp(&pSrcFrame[lower_right], &pSrcFrame[upper_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_right], bytes);
          else if (memcmp(&pSrcFrame[lower_left], &pSrcFrame[upper_right], bytes) == 0 ||
                   memcmp(&pSrcFrame[lower_left], &pSrcFrame[upper_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[lower_left], bytes);
          else if (memcmp(&pSrcFrame[upper_right], &pSrcFrame[upper_left], bytes) == 0)
            memcpy(&pDestFrame[target], &pSrcFrame[upper_right], bytes);
          else
            memcpy(&pDestFrame[target], &pSrcFrame[lower_right], bytes);
        }
      }

      upper_left += 2 * bytes;
      upper_right += 2 * bytes;
      lower_left += 2 * bytes;
      lower_right += 2 * bytes;
      target += bytes;
    }
  }
}

inline void Helper::ScaleUp(uint8_t* pDestFrame, const uint8_t* pSrcFrame, const uint16_t srcWidth,
                            const uint8_t srcHeight, uint8_t bits)
{
  uint8_t bytes = bits / 8;  // RGB24 (3 byte) or RGB16 (2 byte) or indexed (1 byte)
  uint16_t destWidth = srcWidth * 2;
  memset(pDestFrame, 0, srcWidth * srcHeight * 4 * bytes);

  // we implement scale2x http://www.scale2x.it/algorithm
  uint16_t row = srcWidth * bytes;
  uint8_t* a = (uint8_t*)malloc(bytes);
  uint8_t* b = (uint8_t*)malloc(bytes);
  uint8_t* c = (uint8_t*)malloc(bytes);
  uint8_t* d = (uint8_t*)malloc(bytes);
  uint8_t* e = (uint8_t*)malloc(bytes);
  uint8_t* f = (uint8_t*)malloc(bytes);
  uint8_t* g = (uint8_t*)malloc(bytes);
  uint8_t* h = (uint8_t*)malloc(bytes);
  uint8_t* i = (uint8_t*)malloc(bytes);

  for (uint16_t y = 0; y < srcHeight; y++)
  {
    for (uint16_t x = 0; x < srcWidth; x++)
    {
      for (uint8_t tc = 0; tc < bytes; tc++)
      {
        if (x == 0 && y == 0)
        {
          a[tc] = b[tc] = d[tc] = e[tc] = pSrcFrame[tc];
          c[tc] = f[tc] = pSrcFrame[bytes + tc];
          g[tc] = h[tc] = pSrcFrame[row + tc];
          i[tc] = pSrcFrame[row + bytes + tc];
        }
        else if ((x == 0) && (y == srcHeight - 1))
        {
          a[tc] = b[tc] = pSrcFrame[(y - 1) * row + tc];
          c[tc] = pSrcFrame[(y - 1) * row + bytes + tc];
          d[tc] = g[tc] = h[tc] = e[tc] = pSrcFrame[y * row + tc];
          f[tc] = i[tc] = pSrcFrame[y * row + bytes + tc];
        }
        else if ((x == srcWidth - 1) && (y == 0))
        {
          a[tc] = d[tc] = pSrcFrame[x * bytes - bytes + tc];
          b[tc] = c[tc] = f[tc] = e[tc] = pSrcFrame[x * bytes + tc];
          g[tc] = pSrcFrame[row + x * bytes - bytes + tc];
          h[tc] = i[tc] = pSrcFrame[row + x * bytes + tc];
        }
        else if ((x == srcWidth - 1) && (y == srcHeight - 1))
        {
          a[tc] = pSrcFrame[y * row - 2 * bytes + tc];
          b[tc] = c[tc] = pSrcFrame[y * row - bytes + tc];
          d[tc] = g[tc] = pSrcFrame[srcHeight * row - 2 * bytes + tc];
          e[tc] = f[tc] = h[tc] = i[tc] = pSrcFrame[srcHeight * row - bytes + tc];
        }
        else if (x == 0)
        {
          a[tc] = b[tc] = pSrcFrame[(y - 1) * row + tc];
          c[tc] = pSrcFrame[(y - 1) * row + bytes + tc];
          d[tc] = e[tc] = pSrcFrame[y * row + tc];
          f[tc] = pSrcFrame[y * row + bytes + tc];
          g[tc] = h[tc] = pSrcFrame[(y + 1) * row + tc];
          i[tc] = pSrcFrame[(y + 1) * row + bytes + tc];
        }
        else if (x == srcWidth - 1)
        {
          a[tc] = pSrcFrame[y * row - 2 * bytes + tc];
          b[tc] = c[tc] = pSrcFrame[y * row - bytes + tc];
          d[tc] = pSrcFrame[(y + 1) * row - 2 * bytes + tc];
          e[tc] = f[tc] = pSrcFrame[(y + 1) * row - bytes + tc];
          g[tc] = pSrcFrame[(y + 2) * row - 2 * bytes + tc];
          h[tc] = i[tc] = pSrcFrame[(y + 2) * row - bytes + tc];
        }
        else if (y == 0)
        {
          a[tc] = d[tc] = pSrcFrame[x * bytes - bytes + tc];
          b[tc] = e[tc] = pSrcFrame[x * bytes + tc];
          c[tc] = f[tc] = pSrcFrame[x * bytes + bytes + tc];
          g[tc] = pSrcFrame[row + x * bytes - bytes + tc];
          h[tc] = pSrcFrame[row + x * bytes + tc];
          i[tc] = pSrcFrame[row + x * bytes + bytes + tc];
        }
        else if (y == srcHeight - 1)
        {
          a[tc] = pSrcFrame[(y - 1) * row + x * bytes - bytes + tc];
          b[tc] = pSrcFrame[(y - 1) * row + x * bytes + tc];
          c[tc] = pSrcFrame[(y - 1) * row + x * bytes + bytes + tc];
          d[tc] = g[tc] = pSrcFrame[y * row + x * bytes - bytes + tc];
          e[tc] = h[tc] = pSrcFrame[y * row + x * bytes + tc];
          f[tc] = i[tc] = pSrcFrame[y * row + x * bytes + bytes + tc];
        }
        else
        {
          a[tc] = pSrcFrame[(y - 1) * row + x * bytes - bytes + tc];
          b[tc] = pSrcFrame[(y - 1) * row + x * bytes + tc];
          c[tc] = pSrcFrame[(y - 1) * row + x * bytes + bytes + tc];
          d[tc] = pSrcFrame[y * row + x * bytes - bytes + tc];
          e[tc] = pSrcFrame[y * row + x * bytes + tc];
          f[tc] = pSrcFrame[y * row + x * bytes + bytes + tc];
          g[tc] = pSrcFrame[(y + 1) * row + x * bytes - bytes + tc];
          h[tc] = pSrcFrame[(y + 1) * row + x * bytes + tc];
          i[tc] = pSrcFrame[(y + 1) * row + x * bytes + bytes + tc];
        }
      }

      if (memcmp(b, h, bytes) != 0 && memcmp(d, f, bytes) != 0)
      {
        memcpy(&pDestFrame[(y * 2 * destWidth + x * 2) * bytes], memcmp(d, b, bytes) == 0 ? d : e, bytes);
        memcpy(&pDestFrame[(y * 2 * destWidth + x * 2 + 1) * bytes], memcmp(b, f, bytes) == 0 ? f : e, bytes);
        memcpy(&pDestFrame[((y * 2 + 1) * destWidth + x * 2) * bytes], memcmp(d, h, bytes) == 0 ? d : e, bytes);
        memcpy(&pDestFrame[((y * 2 + 1) * destWidth + x * 2 + 1) * bytes], memcmp(h, f, bytes) == 0 ? f : e, bytes);
      }
      else
      {
        memcpy(&pDestFrame[(y * 2 * destWidth + x * 2) * bytes], e, bytes);
        memcpy(&pDestFrame[(y * 2 * destWidth + x * 2 + 1) * bytes], e, bytes);
        memcpy(&pDestFrame[((y * 2 + 1) * destWidth + x * 2) * bytes], e, bytes);
        memcpy(&pDestFrame[((y * 2 + 1) * destWidth + x * 2 + 1) * bytes], e, bytes);
      }
    }
  }

  free(a);
  free(b);
  free(c);
  free(d);
  free(e);
  free(f);
  free(g);
  free(h);
  free(i);
}

inline void Helper::ScaleUpIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, const uint16_t srcWidth,
                                   const uint8_t srcHeight)
{
  ScaleUp(pDestFrame, pSrcFrame, srcWidth, srcHeight, 8);
}

inline void Helper::Center(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                           const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight, uint8_t bits)
{
  uint8_t xOffset = (destWidth - srcWidth) / 2;
  uint8_t yOffset = (destHeight - srcHeight) / 2;
  uint8_t bytes = bits / 8;  // RGB24 (3 byte) or RGB16 (2 byte) or indexed (1 byte)

  memset(pDestFrame, 0, destWidth * destHeight * bytes);

  for (uint8_t y = 0; y < srcHeight; y++)
  {
    memcpy(&pDestFrame[((yOffset + y) * destWidth + xOffset) * bytes], &pSrcFrame[y * srcWidth * bytes],
           srcWidth * bytes);
  }
}

inline void Helper::CenterIndexed(uint8_t* pDestFrame, const uint16_t destWidth, const uint8_t destHeight,
                                  const uint8_t* pSrcFrame, const uint16_t srcWidth, const uint8_t srcHeight)
{
  Center(pDestFrame, destWidth, destHeight, pSrcFrame, srcWidth, srcHeight, 8);
}

inline uint8_t Helper::ReverseByte(uint8_t value)
{
  return kReverseByte[value];
}

inline uint32_t Helper::Checksum(const uint8_t* input, size_t len, bool reverse)
{
  uint32_t cs = 0xFFFFFFFFu;
  if (!reverse)
  {
    for (size_t i = 0; i < len; i++)
    {
      cs = (cs >> 8) ^ kChecksumTable[(cs ^ input[i]) & 0xFFu];
    }
  }
  else
  {
    for (size_t i = 0; i < len; i++)
    {
      cs = (cs >> 8) ^ kChecksumTable[(cs ^ kReverseByte[input[i]]) & 0xFFu];
    }
  }
  return cs ^ 0xFFFFFFFFu;
}

inline uint32_t Helper::ChecksumWithMask(const uint8_t* input, const uint8_t* mask, size_t len, bool reverse)
{
  uint32_t cs = 0xFFFFFFFFu;
  if (!reverse)
  {
    for (size_t i = 0; i < len; i++)
    {
      cs = (cs >> 8) ^ kChecksumTable[(cs ^ (input[i] & mask[i])) & 0xFFu];
    }
  }
  else
  {
    for (size_t i = 0; i < len; i++)
    {
      cs = (cs >> 8) ^ kChecksumTable[(cs ^ (kReverseByte[input[i]] & mask[i])) & 0xFFu];
    }
  }
  return cs ^ 0xFFFFFFFFu;
}

inline void Helper::Join(uint8_t* pFrame, uint16_t width, uint16_t height, uint8_t bitlen, const uint8_t* pPlanes)
{
  int planeSize = width * height / 8;
  std::vector<const uint8_t*> planes(bitlen, nullptr);
  for (uint8_t i = 0; i < bitlen; i++)
  {
    planes[i] = pPlanes + i * planeSize;
  }

  int byteIdx = 0;
  uint8_t andValue = 1;
  int total = width * height;
  for (int i = 0; i < total; i++)
  {
    uint8_t value = 0;
    for (uint8_t p = 0; p < bitlen; p++)
    {
      if (planes[p][byteIdx] & andValue)
      {
        value |= static_cast<uint8_t>(1u << p);
      }
    }
    pFrame[i] = value;

    if (andValue == 0x80)
    {
      andValue = 0x01;
      byteIdx++;
    }
    else
    {
      andValue <<= 1;
    }
  }
}

inline std::vector<uint8_t> Helper::NewPlane(uint16_t width, uint16_t height)
{
  size_t count = static_cast<size_t>(width / 8) * height;
  return std::vector<uint8_t>(count);
}

inline void Helper::ClearPlane(uint8_t* plane, size_t len)
{
  memset(plane, 0, len);
}

inline void Helper::OrPlane(const uint8_t* plane, uint8_t* target, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    target[i] = static_cast<uint8_t>(target[i] | plane[i]);
  }
}

inline void Helper::CombinePlaneWithMask(const uint8_t* planeA, const uint8_t* planeB, const uint8_t* mask,
                                        uint8_t* out, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    out[i] = static_cast<uint8_t>((planeA[i] & mask[i]) | (planeB[i] & ~mask[i]));
  }
}

inline void Helper::ScaleDoubleIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, uint16_t srcWidth, uint16_t srcHeight)
{
  uint16_t destWidth = srcWidth * 2;
  uint16_t destHeight = srcHeight * 2;
  for (uint16_t y = 0; y < srcHeight; y++)
  {
    for (uint16_t x = 0; x < srcWidth; x++)
    {
      uint8_t value = pSrcFrame[y * srcWidth + x];
      uint16_t outX = x * 2;
      uint16_t outY = y * 2;
      pDestFrame[outY * destWidth + outX] = value;
      pDestFrame[outY * destWidth + outX + 1] = value;
      pDestFrame[(outY + 1) * destWidth + outX] = value;
      pDestFrame[(outY + 1) * destWidth + outX + 1] = value;
    }
  }
}

inline void Helper::Scale2XIndexed(uint8_t* pDestFrame, const uint8_t* pSrcFrame, uint16_t srcWidth, uint16_t srcHeight)
{
  auto getPixel = [&](int x, int y) -> uint8_t {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= srcWidth) x = srcWidth - 1;
    if (y >= srcHeight) y = srcHeight - 1;
    return pSrcFrame[y * srcWidth + x];
  };

  uint16_t destWidth = srcWidth * 2;
  for (uint16_t y = 0; y < srcHeight; y++)
  {
    for (uint16_t x = 0; x < srcWidth; x++)
    {
      uint8_t b = getPixel(x, static_cast<int>(y) - 1);
      uint8_t h = getPixel(x, static_cast<int>(y) + 1);
      uint8_t d = getPixel(static_cast<int>(x) - 1, y);
      uint8_t f = getPixel(static_cast<int>(x) + 1, y);
      uint8_t e = getPixel(x, y);

      uint8_t e0 = e;
      uint8_t e1 = e;
      uint8_t e2 = e;
      uint8_t e3 = e;
      if (b != h && d != f)
      {
        e0 = (d == b) ? d : e;
        e1 = (b == f) ? f : e;
        e2 = (d == h) ? d : e;
        e3 = (h == f) ? f : e;
      }

      uint16_t outX = x * 2;
      uint16_t outY = y * 2;
      pDestFrame[outY * destWidth + outX] = e0;
      pDestFrame[outY * destWidth + outX + 1] = e1;
      pDestFrame[(outY + 1) * destWidth + outX] = e2;
      pDestFrame[(outY + 1) * destWidth + outX + 1] = e3;
    }
  }
}

}  // namespace FrameUtil
