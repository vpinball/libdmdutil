#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#define DMDUTIL_FRAME_BUFFER_SIZE 16
#define DMDUTIL_MAX_NAME_SIZE 32
#define DMDUTIL_MAX_TRANSITIONAL_FRAME_DURATION 25

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
class ConsoleDMD;

class DMDUTILAPI DMD
{
 public:
  DMD();
  ~DMD();

  void FindDisplays();
  static bool IsFinding();
  bool HasDisplay() const;
  void DumpDMDTxt();
  void DumpDMDRaw();
  LevelDMD* CreateLevelDMD(uint16_t width, uint16_t height, bool sam);
  bool DestroyLevelDMD(LevelDMD* pLevelDMD);
  RGB24DMD* CreateRGB24DMD(uint16_t width, uint16_t height);
  bool DestroyRGB24DMD(RGB24DMD* pRGB24DMD);
  ConsoleDMD* CreateConsoleDMD(bool overwrite, FILE* out = stdout);
  bool DestroyConsoleDMD(ConsoleDMD* pConsoleDMD);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  const char* name = nullptr);
  void UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                       uint8_t b);
  void UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height);
  void UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height);
  void UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                              uint8_t g, uint8_t b, const char* name = nullptr);

 private:
  enum class DMDMode
  {
    Unknown,
    Data,
    RGB24,  // RGB888
    RGB16,  // RGB565
    AlphaNumeric
  };

  struct DMDUpdate
  {
    DMDMode mode;
    AlphaNumericLayout layout;
    int depth;
    uint8_t data[256 * 64 * 3];
    uint16_t segData[256 * 64];  // RGB16 or segment data
    uint16_t segData2[128];
    bool hasData;
    bool hasSegData;
    bool hasSegData2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t width;
    uint16_t height;
    char name[DMDUTIL_MAX_NAME_SIZE];
  };

  DMDUpdate* m_updateBuffer[DMDUTIL_FRAME_BUFFER_SIZE];

  bool UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  DMDMode node, const char* name);
  void AdjustRGB24Depth(uint8_t* pData, uint8_t* pDstData, int length, uint8_t* palette, uint8_t depth);

  void DmdFrameReadyResetThread();
  void LevelDMDThread();
  void RGB24DMDThread();
  void ConsoleDMDThread();
  void ZeDMDThread();
  void DumpDMDTxtThread();
  void DumpDMDRawThread();

  uint8_t m_updateBufferPosition = 0;
  AlphaNumeric* m_pAlphaNumeric;
  Serum* m_pSerum;
  ZeDMD* m_pZeDMD;
  std::vector<LevelDMD*> m_levelDMDs;
  std::vector<RGB24DMD*> m_rgb24DMDs;
  std::vector<ConsoleDMD*> m_consoleDMDs;

  std::thread* m_pLevelDMDThread;
  std::thread* m_pRGB24DMDThread;
  std::thread* m_pConsoleDMDThread;
  std::thread* m_pZeDMDThread;
  std::thread* m_pdmdFrameReadyResetThread;
  std::thread* m_pDumpDMDTxtThread;
  std::thread* m_pDumpDMDRawThread;
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
