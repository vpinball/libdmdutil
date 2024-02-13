#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#define DMD_FRAME_BUFFER_SIZE 16

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

class ZeDMD;

namespace DMDUtil
{

enum class AlphaNumericLayout
{
  NoLayout,
  __2x16Alpha,
  __2x20Alpha,
  __2x7Alpha_2x7Num,
  __2x7Alpha_2x7Num_4x1Num,
  __2x7Num_2x7Num_4x1Num,
  __2x7Num_2x7Num_10x1Num,
  __2x7Num_2x7Num_4x1Num_gen7,
  __2x7Num10_2x7Num10_4x1Num,
  __2x6Num_2x6Num_4x1Num,
  __2x6Num10_2x6Num10_4x1Num,
  __4x7Num10,
  __6x4Num_4x1Num,
  __2x7Num_4x1Num_1x16Alpha,
  __1x16Alpha_1x16Num_1x7Num,
  __1x7Num_1x16Alpha_1x16Num,
  __1x16Alpha_1x16Num_1x7Num_1x4Num
};

class AlphaNumeric;
class Serum;
class PixelcadeDMD;
class LevelDMD;
class RGB24DMD;

class DMDUTILAPI DMD
{
 public:
  DMD();
  ~DMD();

  static bool IsFinding();
  bool HasDisplay() const;
  LevelDMD* CreateLevelDMD(uint16_t width, uint16_t height, bool sam);
  bool DestroyLevelDMD(LevelDMD* pLevelDMD);
  RGB24DMD* CreateRGB24DMD(uint16_t width, uint16_t height);
  bool DestroyRGB24DMD(RGB24DMD* pRGB24DMD);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  const char* name = nullptr);
  void UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                       uint8_t b);
  void UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b);
  void UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                              uint8_t g, uint8_t b, const char* name = nullptr);

 private:
  enum class DMDMode
  {
    Unknown,
    Data,
    RGB24,
    AlphaNumeric
  };

  struct DMDUpdate
  {
    DMDMode mode;
    AlphaNumericLayout layout;
    int depth;
    uint8_t data[256 * 64 * 3];
    uint16_t segData[128];
    uint16_t segData2[128];
    bool hasSegData2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t width;
    uint16_t height;
    const char* name;
  };

  DMDUpdate* m_updateBuffer[DMD_FRAME_BUFFER_SIZE];

  void FindDevices();
  void Run();
  void Stop();
  bool UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  DMDMode node, const char* name);
  void AdjustRGB24Depth(uint8_t* pData, uint8_t* pDstData, int length, uint8_t* palette, uint8_t depth);

  void DmdFrameReadyResetThread();
  void LevelDMDThread();
  void RGB24DMDThread();
  void ZeDMDThread();

  uint8_t m_updateBufferPosition = 0;
  AlphaNumeric* m_pAlphaNumeric;
  Serum* m_pSerum;
  ZeDMD* m_pZeDMD;
  std::vector<LevelDMD*> m_levelDMDs;
  std::vector<RGB24DMD*> m_rgb24DMDs;

  std::thread* m_pLevelDMDThread;
  std::thread* m_pRGB24DMDThread;
  std::thread* m_pZeDMDThread;
  std::thread* m_pdmdFrameReadyResetThread;
  std::shared_mutex m_dmdSharedMutex;
  std::condition_variable_any m_dmdCV;
  std::atomic<bool> m_dmdFrameReady = false;
  std::atomic<bool> m_stopFlag = false;

  static bool m_finding;

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  void PixelcadeDMDThread();
  PixelcadeDMD* m_pPixelcadeDMD;
  std::thread* m_pPixelcadeDMDThread;
#endif
};

}  // namespace DMDUtil
