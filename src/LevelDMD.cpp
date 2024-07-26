#include "DMDUtil/LevelDMD.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace DMDUtil
{

LevelDMD::LevelDMD(uint16_t width, uint16_t height, bool sam)
{
  m_width = width;
  m_height = height;
  m_length = (int)width * height;
  m_pitch = width;
  m_sam = sam;

  m_pData = (uint8_t*)malloc(m_length);
  memset(m_pData, 0, m_length);

  m_update = false;
}

LevelDMD::~LevelDMD() { free(m_pData); }

void LevelDMD::Update(uint8_t* pLevelData, uint8_t depth)
{
  memcpy(m_pData, pLevelData, m_length);
  if (depth == 2)
  {
    for (int i = 0; i < m_length; i++) m_pData[i] = LEVELS_WPC[pLevelData[i]];
    m_update = true;
  }
  else if (depth == 4)
  {
    if (m_sam)
    {
      for (int i = 0; i < m_length; i++) m_pData[i] = LEVELS_SAM[pLevelData[i]];
    }
    else
    {
      for (int i = 0; i < m_length; i++) m_pData[i] = LEVELS_GTS3[pLevelData[i]];
    }
    m_update = true;
  }
}

uint8_t* LevelDMD::GetData()
{
  if (!m_update) return nullptr;

  m_update = false;
  return m_pData;
}

}  // namespace DMDUtil
