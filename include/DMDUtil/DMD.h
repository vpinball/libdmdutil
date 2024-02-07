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
class VirtualDMD;

class DMDUTILAPI DMD
{
 public:
  DMD(int width, int height, bool sam = false, const char* name = nullptr);
  ~DMD();

  static bool IsFinding();
  bool HasDisplay() const;
  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  int GetLength() const { return m_length; }
  VirtualDMD* CreateVirtualDMD();
  bool DestroyVirtualDMD(VirtualDMD* pVirtualDMD);
  void UpdateData(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateRGB24Data(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                              uint8_t g, uint8_t b);

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
    void* pData;
    void* pData2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t width;
    uint16_t height;
  };

  DMDUpdate* m_updateBuffer[DMD_FRAME_BUFFER_SIZE];

  static constexpr uint8_t LEVELS_WPC[] = {0x14, 0x21, 0x43, 0x64};
  static constexpr uint8_t LEVELS_GTS3[] = {0x00, 0x1E, 0x23, 0x28, 0x2D, 0x32, 0x37, 0x3C,
                                            0x41, 0x46, 0x4B, 0x50, 0x55, 0x5A, 0x5F, 0x64};
  static constexpr uint8_t LEVELS_SAM[] = {0x00, 0x14, 0x19, 0x1E, 0x23, 0x28, 0x2D, 0x32,
                                           0x37, 0x3C, 0x41, 0x46, 0x4B, 0x50, 0x5A, 0x64};

  void FindDevices();
  void Run();
  void Stop();
  bool UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateData(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b, DMDMode node);

  void DmdFrameReadyResetThread();
  void ZeDMDThread();

  int m_width;
  int m_height;
  int m_length;
  bool m_sam;
  uint8_t* m_pBuffer;
  uint8_t* m_pRGB24Buffer;
  uint16_t m_segData1[128];
  uint16_t m_segData2[128];
  uint8_t* m_pLevelData;
  uint8_t* m_pRGB24Data;
  uint16_t* m_pRGB565Data;
  uint8_t m_palette[192];
  uint8_t m_updateBufferPosition = 0;
  AlphaNumeric* m_pAlphaNumeric;
  Serum* m_pSerum;
  ZeDMD* m_pZeDMD;

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

  std::vector<VirtualDMD*> m_virtualDMDs;
};

}  // namespace DMDUtil
