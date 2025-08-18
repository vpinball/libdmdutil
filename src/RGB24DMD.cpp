#include "DMDUtil/RGB24DMD.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "FrameUtil.h"

namespace DMDUtil
{

RGB24DMD::RGB24DMD(uint16_t width, uint16_t height)
{
  m_width = width;
  m_height = height;
  m_length = (int)width * height * 3;
  m_pitch = width * 3;

  m_pData = (uint8_t*)malloc(m_length);
  memset(m_pData, 0, m_length);

  m_update = false;
}

RGB24DMD::~RGB24DMD() { free(m_pData); }

void RGB24DMD::Update(uint8_t* pData, uint16_t width, uint16_t height)
{
  if (width == 0) width = m_width;
  if (height == 0) height = m_height;

  if (width == m_width && height == m_height)
  {
    memcpy(m_pData, pData, m_length);
    m_update = true;
  }
  else if (width == 128 && height == 16 && m_width == 128 && m_height == 32)
  {
    FrameUtil::Helper::Center(m_pData, 128, 32, pData, 128, 16, 24);
    m_update = true;
  }
  else if (height == 64 && m_height == 32)
  {
    FrameUtil::Helper::ScaleDown(m_pData, 128, 32, pData, width, height, 24);
    m_update = true;
  }
  else if (width == 128 && height == 32 && m_width == 256 && m_height == 64)
  {
    FrameUtil::Helper::ScaleUp(m_pData, pData, width, height, 24);
    m_update = true;
  }
}

uint8_t* RGB24DMD::GetData()
{
  if (!m_update) return nullptr;

  m_update = false;
  return m_pData;
}

}  // namespace DMDUtil
