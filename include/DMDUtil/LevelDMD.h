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

class DMDUTILAPI LevelDMD
{
 public:
  LevelDMD(uint16_t width, uint16_t height, bool sam);
  ~LevelDMD();

  void Update(uint8_t* pLevelData, uint8_t depth);
  int GetWidth() { return m_width; }
  int GetHeight() { return m_height; }
  int GetLength() const { return m_length; }
  int GetPitch() const { return m_pitch; }
  uint8_t* GetData();

 private:
  static constexpr uint8_t LEVELS_WPC[] = {0x14, 0x21, 0x43, 0x64};
  static constexpr uint8_t LEVELS_GTS3[] = {0x00, 0x1E, 0x23, 0x28, 0x2D, 0x32, 0x37, 0x3C,
                                            0x41, 0x46, 0x4B, 0x50, 0x55, 0x5A, 0x5F, 0x64};
  static constexpr uint8_t LEVELS_SAM[] = {0x00, 0x14, 0x19, 0x1E, 0x23, 0x28, 0x2D, 0x32,
                                           0x37, 0x3C, 0x41, 0x46, 0x4B, 0x50, 0x5A, 0x64};

  uint16_t m_width;
  uint16_t m_height;
  int m_length;
  int m_pitch;
  int m_update;
  bool m_sam;

  uint8_t* m_pData;
};

}  // namespace DMDUtil