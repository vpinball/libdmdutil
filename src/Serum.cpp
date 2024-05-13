#include "Serum.h"

#include <cstring>

#include "Logger.h"
#include "serum-decode.h"

namespace DMDUtil
{

bool Serum::m_isLoaded = false;

Serum::Serum(unsigned int width32, unsigned int width64)
{
  m_width32 = (uint8_t)width32;
  m_width64 = (uint16_t)width64;
}

Serum::~Serum()
{
  Serum_Dispose();

  m_isLoaded = false;
}

Serum* Serum::Load(const char* const altColorPath, const char* const romName)
{
  if (m_isLoaded) return nullptr;

  unsigned int width32;
  unsigned int width64;
  unsigned int numColors;
  unsigned int numTriggers;
  uint8_t isNewFormat;

  if (Serum_Load(altColorPath, romName, &numColors, &numTriggers, FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES, &width32, &width64, &isNewFormat))
  {
    Serum_Dispose();
    return nullptr;
  }

  Log(DMDUtil_LogLevel_INFO, "Serum loaded: romName=%s, width32=%d, width64=%d, numColors=%d, numTriggers=%d, isNewFormat=%d", romName, width32, width64, numColors,
      numTriggers, isNewFormat);

  m_isLoaded = true;
  m_isNewFormat = (bool) isNewFormat;

  return new Serum(width, height);
}

bool Serum::Convert(uint8_t* pFrame, uint8_t* pDstFrame, uint8_t* pDstPalette, uint16_t width, uint16_t height,
                    uint32_t* triggerID)
{
  if (pFrame) memcpy(pDstFrame, pFrame, width * height);

  return Serum_ColorizeOrApplyRotations(pDstFrame ? pDstFrame : nullptr, width, height, pDstPalette, triggerID);
};

void Serum::SetStandardPalette(const uint8_t* palette, const int bitDepth)
{
  Serum_SetStandardPalette(palette, bitDepth);
}

bool Serum::ColorizeWithMetadata(uint8_t* frame, int width, int height, uint8_t* palette, uint8_t* rotations,
                                 uint32_t* triggerID, uint32_t* hashcode, int* frameID)
{
  return Serum_ColorizeWithMetadata(frame, width, height, palette, rotations, triggerID, hashcode, frameID);
}

void Serum::SetIgnoreUnknownFramesTimeout(int milliseconds)
{
  return Serum_SetIgnoreUnknownFramesTimeout((uint16_t)milliseconds);
}

void Serum::SetMaximumUnknownFramesToSkip(int maximum) { return Serum_SetMaximumUnknownFramesToSkip((uint8_t)maximum); }

}  // namespace DMDUtil
