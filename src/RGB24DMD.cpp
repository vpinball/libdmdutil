#include "DMDUtil/RGB24DMD.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace DMDUtil
{

RGB24DMD::RGB24DMD(uint16_t width, uint16_t height)
{
  m_width = width;
  m_height = height;
  m_length = width * height * 3;
  m_pitch = width * 3;

  m_pData = (uint8_t*)malloc(m_length);
  memset(m_pData, 0, m_length);

  m_update = false;
}

RGB24DMD::~RGB24DMD() { free(m_pData); }

void RGB24DMD::Update(uint8_t* pData)
{
  memcpy(m_pData, pData, m_length);

  m_update = true;
}

uint8_t* RGB24DMD::GetData()
{
  if (!m_update) return nullptr;

  m_update = false;
  return m_pData;
}

}  // namespace DMDUtil