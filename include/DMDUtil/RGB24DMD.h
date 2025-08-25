#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdint>

namespace DMDUtil
{

class DMDUTILAPI RGB24DMD
{
 public:
  RGB24DMD(uint16_t width, uint16_t height);
  ~RGB24DMD();

  virtual void Update(uint8_t* pRGB24Data, uint16_t width = 0, uint16_t height = 0);
  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  int GetLength() const { return m_length; }
  int GetPitch() const { return m_pitch; }
  uint8_t* GetData();

 protected:
  uint16_t m_width;
  uint16_t m_height;
  int m_length;
  int m_pitch;
  bool m_update;

  uint8_t* m_pData;
};

}  // namespace DMDUtil