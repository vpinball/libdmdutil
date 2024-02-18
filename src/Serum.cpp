#include "Serum.h"

#include <cstring>

#include "DMDUtil/Config.h"
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

Serum* Serum::Load(const std::string& romName)
{
  if (m_isLoaded) return nullptr;

  std::string altColorPath = Config::GetInstance()->GetAltColorPath();

  if (altColorPath.empty()) return nullptr;

  int width;
  int height;
  unsigned int numColors;
  unsigned int numTriggers;

  if (!Serum_Load(altColorPath.c_str(), romName.c_str(), &width, &height, &numColors, &numTriggers))
  {
    Serum_Dispose();
    return nullptr;
  }

  Log("Serum loaded: romName=%s, width=%d, height=%d, numColors=%d, numTriggers=%d", romName.c_str(), width, height,
      numColors, numTriggers);

  m_isLoaded = true;

  return new Serum(width, height);
}

bool Serum::Convert(uint8_t* pFrame, uint8_t* pDstFrame, uint8_t* pDstPalette, uint16_t width, uint16_t height)
{
  if (pFrame) memcpy(pDstFrame, pFrame, width * height);

  unsigned int triggerId;

  return Serum_ColorizeOrApplyRotations(pDstFrame ? pDstFrame : nullptr, width, height, pDstPalette, &triggerId);
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

}  // namespace DMDUtil
