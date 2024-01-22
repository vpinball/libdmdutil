#include "Serum.h"

#include <cstring>

#include "DMDUtil/Config.h"
#include "Logger.h"
#include "serum-decode.h"

namespace DMDUtil {

bool Serum::m_isLoaded = false;

Serum::Serum(int width, int height) {
  m_width = width;
  m_height = height;
  m_length = width * height;
  m_pFrame = (uint8_t*)malloc(m_length);
  memset(m_pFrame, 0, m_length);
}

Serum::~Serum() {
  free(m_pFrame);

  Serum_Dispose();

  m_isLoaded = false;
}

Serum* Serum::Load(const std::string& romName) {
  if (m_isLoaded) return nullptr;

  std::string altColorPath = Config::GetInstance()->GetAltColorPath();

  if (altColorPath.empty()) return nullptr;

  int width;
  int height;
  unsigned int numColors;
  unsigned int numTriggers;

  if (!Serum_Load(altColorPath.c_str(), romName.c_str(), &width, &height,
                  &numColors, &numTriggers)) {
    Serum_Dispose();
    return nullptr;
  }

  Log("Serum loaded: romName=%s, width=%d, height=%d, numColors=%d, "
      "numTriggers=%d",
      romName.c_str(), width, height, numColors, numTriggers);

  m_isLoaded = true;

  return new Serum(width, height);
}

bool Serum::Convert(uint8_t* pFrame, uint8_t* pDstFrame, uint8_t* pDstPalette) {
  if (pFrame) memcpy(m_pFrame, pFrame, m_length);

  unsigned int triggerId;

  if (Serum_ColorizeOrApplyRotations(pFrame ? m_pFrame : nullptr, m_width,
                                     m_height, m_palette, &triggerId)) {
    memcpy(pDstFrame, m_pFrame, m_length);
    memcpy(pDstPalette, m_palette, 192);

    return true;
  }

  return false;
};

}  // namespace DMDUtil
