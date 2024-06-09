#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#define DMDUTIL_FRAME_BUFFER_SIZE 32
#define DMDUTIL_MAX_FRAME_BUFFER_SIZE (16 * DMDUTIL_FRAME_BUFFER_SIZE)
#define DMDUTIL_MAX_NAME_SIZE 16
#define DMDUTIL_MAX_PATH_SIZE 256
#define DMDUTIL_MAX_TRANSITIONAL_FRAME_DURATION 25

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>

#include "sockpp/tcp_connector.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

class ZeDMD;

struct _Serum_Frame_Struc;
typedef _Serum_Frame_Struc SerumFrameStruct;

namespace PUPDMD
{
class DMD;
}

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

  enum class Mode
  {
    Unknown,        // int 0
    Data,           // int 1
    RGB24,          // int 2, RGB888
    RGB16,          // int 3, RGB565
    AlphaNumeric,   // int 4
    SerumV1,        // int 5
    SerumV2_32,     // int 6
    SerumV2_32_64,  // int 7
    SerumV2_64,     // int 8
    SerumV2_64_32,  // int 9
  };

  bool IsSerumMode(Mode mode)
  {
    return (mode == Mode::SerumV1 || mode == Mode::SerumV2_32 || mode == Mode::SerumV2_32_64 ||
            mode == Mode::SerumV2_64 || mode == Mode::SerumV2_64_32);
  }

  bool IsSerumV2Mode(Mode mode)
  {
    return (mode == Mode::SerumV2_32 || mode == Mode::SerumV2_32_64 || mode == Mode::SerumV2_64 ||
            mode == Mode::SerumV2_64_32);
  }

#pragma pack(push, 1)  // Align to 1-byte boundaries, important for sending over socket.
  struct Update
  {
    Mode mode;
    AlphaNumericLayout layout;
    int depth;
    uint8_t data[256 * 64 * 3];
    uint16_t segData[256 * 64];  // RGB16 or segment data or SerumV1 palette
    uint16_t segData2[128];
    bool hasData;
    bool hasSegData;
    bool hasSegData2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t width;
    uint16_t height;
  };

  struct StreamHeader
  {
    char header[10] = "DMDStream";
    uint8_t version = 1;
    Mode mode = Mode::Data;  // int
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t buffered = 0;          // 0 => unbuffered, 1 => buffered
    uint8_t disconnectOthers = 0;  // 0 => no, 1 => yes
    uint32_t length = 0;
  };

  struct PathsHeader
  {
    char header[6] = "Paths";
    char name[DMDUTIL_MAX_NAME_SIZE] = {0};
    char altColorPath[DMDUTIL_MAX_PATH_SIZE] = {0};
    char pupVideosPath[DMDUTIL_MAX_PATH_SIZE] = {0};
  };
#pragma pack(pop)  // Reset to default packing

  void FindDisplays();
  static bool IsFinding();
  bool HasDisplay() const;
  bool HasHDDisplay() const;
  void SetRomName(const char* name);
  void SetAltColorPath(const char* path);
  void SetPUPVideosPath(const char* path);
  void DumpDMDTxt();
  void DumpDMDRaw();
  LevelDMD* CreateLevelDMD(uint16_t width, uint16_t height, bool sam);
  bool DestroyLevelDMD(LevelDMD* pLevelDMD);
  RGB24DMD* CreateRGB24DMD(uint16_t width, uint16_t height);
  bool DestroyRGB24DMD(RGB24DMD* pRGB24DMD);
  ConsoleDMD* CreateConsoleDMD(bool overwrite, FILE* out = stdout);
  bool DestroyConsoleDMD(ConsoleDMD* pConsoleDMD);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  bool buffered = false);
  void UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                       uint8_t b, bool buffer = false);
  void UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, bool buffered = false);
  void UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height, bool buffered = false);
  void UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                              uint8_t g, uint8_t b);
  void QueueUpdate(Update dmdUpdate, bool buffered);
  bool QueueBuffer();

 private:
  Update* m_pUpdateBufferQueue[DMDUTIL_FRAME_BUFFER_SIZE];
  Update m_updateBuffered;

  int GetNextBufferQueuePosition(int bufferPosition);
  bool ConnectDMDServer();
  bool UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  Mode mode, bool buffered = false);
  void AdjustRGB24Depth(uint8_t* pData, uint8_t* pDstData, int length, uint8_t* palette, uint8_t depth);
  void HandleTrigger(uint16_t id);
  void QueueSerumFrames(Update* dmdUpdate, bool render32 = true, bool render64 = true);

  void DmdFrameThread();
  void LevelDMDThread();
  void RGB24DMDThread();
  void ConsoleDMDThread();
  void ZeDMDThread();
  void DumpDMDTxtThread();
  void DumpDMDRawThread();
  void PupDMDThread();
  void SerumThread();

  char m_romName[DMDUTIL_MAX_NAME_SIZE] = {0};
  char m_altColorPath[DMDUTIL_MAX_PATH_SIZE] = {0};
  char m_pupVideosPath[DMDUTIL_MAX_PATH_SIZE] = {0};
  AlphaNumeric* m_pAlphaNumeric;
  SerumFrameStruct* m_pSerum;
  ZeDMD* m_pZeDMD;
  PUPDMD::DMD* m_pPUPDMD;
  std::vector<LevelDMD*> m_levelDMDs;
  std::vector<RGB24DMD*> m_rgb24DMDs;
  std::vector<ConsoleDMD*> m_consoleDMDs;
  sockpp::tcp_connector* m_pDMDServerConnector;
  bool m_dmdServerDisconnectOthers = false;

  std::thread* m_pLevelDMDThread;
  std::thread* m_pRGB24DMDThread;
  std::thread* m_pConsoleDMDThread;
  std::thread* m_pZeDMDThread;
  std::thread* m_pDmdFrameThread;
  std::thread* m_pDumpDMDTxtThread;
  std::thread* m_pDumpDMDRawThread;
  std::thread* m_pPupDMDThread;
  std::thread* m_pSerumThread;
  std::shared_mutex m_dmdSharedMutex;
  std::condition_variable_any m_dmdCV;
  std::atomic<bool> m_dmdFrameReady;
  std::atomic<bool> m_stopFlag;
  std::atomic<uint16_t> m_updateBufferQueuePosition;

  bool m_hasUpdateBuffered = false;
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
