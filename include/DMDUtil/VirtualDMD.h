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

class DMDUTILAPI VirtualDMD
{
 public:
  VirtualDMD(int width, int height);
  ~VirtualDMD();

  void Update(uint8_t* pLevelData, uint8_t* pRGB24Data);
  int GetWidth() { return m_width; }
  int GetHeight() { return m_height; }
  int GetLength() const { return m_length; }
  int GetPitch() const { return m_pitch; }
  uint8_t* GetLevelData();
  uint8_t* GetRGB24Data();

 private:
  int m_width;
  int m_height;
  int m_length;
  int m_pitch;
  int m_update;

  uint8_t* m_pLevelData;
  uint8_t* m_pRGB24Data;
};

}  // namespace DMDUtil