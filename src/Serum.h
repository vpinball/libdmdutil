#pragma once

#include <cstdint>

namespace DMDUtil
{

class Serum
{
 public:
  ~Serum();

  static Serum* Load(const char* const altColorPath, const char* const romName);
  bool Convert(uint8_t* pFrame, uint8_t* pDstFrame, uint8_t* pDstPalette, uint16_t width, uint16_t height,
               uint32_t* triggerID);
  void SetStandardPalette(const uint8_t* palette, const int bitDepth);
  bool ColorizeWithMetadata(uint8_t* frame, int width, int height, uint8_t* palette, uint8_t* rotations,
                            uint32_t* triggerID, uint32_t* hashcode, int* frameID);
  void SetIgnoreUnknownFramesTimeout(int milliseconds);
  void SetMaximumUnknownFramesToSkip(int maximum);

 private:
  Serum(unsigned int width32, unsigned int width64);

  uint8_t m_width32;
  uint16_t m_width64;
  int m_length;

  static bool m_isLoaded;
  static bool m_isNewFormat;
};

}  // namespace DMDUtil
