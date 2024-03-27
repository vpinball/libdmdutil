#include "Serum.h"

#include <cstring>

#include "Logger.h"
#include "serum-decode.h"

namespace DMDUtil
{

bool Serum::m_isLoaded = false;

Serum::Serum(int width, int height)
{
  m_width = width;
  m_height = height;
  m_length = width * height;
}

Serum::~Serum()
{
  Serum_Dispose();

  m_isLoaded = false;
}

Serum* Serum::Load(const char* const altColorPath, const char* const romName)
{
  if (m_isLoaded) return nullptr;

  int width;
  int height;
  unsigned int numColors;
  unsigned int numTriggers;

  if (!Serum_Load(altColorPath, romName, &width, &height, &numColors, &numTriggers))
  {
    Serum_Dispose();
    return nullptr;
  }

  Log("Serum loaded: romName=%s, width=%d, height=%d, numColors=%d, numTriggers=%d", romName, width, height, numColors,
      numTriggers);

  m_isLoaded = true;

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
