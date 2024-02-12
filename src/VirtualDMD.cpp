#include "DMDUtil/VirtualDMD.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace DMDUtil
{

VirtualDMD::VirtualDMD(uint16_t width, uint16_t height)
{
  m_width = width;
  m_height = height;
  m_length = width * height;
  m_pitch = width * 3;

  m_pLevelData = (uint8_t*)malloc(m_length);
  memset(m_pLevelData, 0, m_length);

  m_pRGB24Data = (uint8_t*)malloc(m_length * 3);
  memset(m_pRGB24Data, 0, m_length * 3);

  m_update = false;
}

VirtualDMD::~VirtualDMD()
{
  free(m_pLevelData);
  free(m_pRGB24Data);
}

void VirtualDMD::Update(uint8_t* pRGB24Data)
{
  memcpy(m_pRGB24Data, pRGB24Data, m_length * 3);

  m_update = true;
}

void VirtualDMD::UpdateLevel(uint8_t* pLevelData)
{
  memcpy(m_pLevelData, pLevelData, m_length);

  m_update = true;
}

uint8_t* VirtualDMD::GetLevelData()
{
  if (!m_update) return nullptr;

  m_update = false;
  return m_pLevelData;
}

uint8_t* VirtualDMD::GetRGB24Data()
{
  if (!m_update) return nullptr;

  m_update = false;
  return m_pRGB24Data;
}

}  // namespace DMDUtil