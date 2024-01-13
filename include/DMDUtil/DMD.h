#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdint>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

class ZeDMD;

namespace DMDUtil {

enum class AlphaNumericLayout {
   None,
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
class Pixelcade;

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
   bool IsUpdated() const { return m_updated; }
   void ResetUpdated() { m_updated = false; }
   void UpdateData(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b);
   void UpdateRGB24Data(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b);
   void UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r, uint8_t g, uint8_t b);
   const uint8_t* GetLevelData() const { return m_pLevelData; }
   const uint32_t* GetRGB32Data() const { return m_pRGB32Data; }

private:
   enum class DmdMode {
      Unknown,
      Data,
      RGB24,
      AlphaNumeric
   };

   struct DMDUpdate {
      DmdMode mode;
      AlphaNumericLayout layout;
      int depth;
      void* pData;
      void* pData2;
      uint8_t r;
      uint8_t g;
      uint8_t b;
   };

   static constexpr uint8_t LEVELS_WPC[] = { 0x14, 0x21, 0x43, 0x64 };
   static constexpr uint8_t LEVELS_GTS3[] = { 0x00, 0x1E, 0x23, 0x28, 0x2D, 0x32, 0x37, 0x3C, 0x41, 0x46, 0x4B, 0x50, 0x55, 0x5A, 0x5F, 0x64 };
   static constexpr uint8_t LEVELS_SAM[] = { 0x00, 0x14, 0x19, 0x1E, 0x23, 0x28, 0x2D, 0x32, 0x37, 0x3C, 0x41, 0x46, 0x4B, 0x50, 0x5A, 0x64 };

   void FindDevices();
   void Run();
   void Stop();
   bool UpdatePalette(const DMDUpdate* pUpdate);
   void UpdateData(const DMDUpdate* pUpdate, bool update);
   void UpdateRGB24Data(const DMDUpdate* pUpdate, bool update);
   void UpdateAlphaNumericData(const DMDUpdate* pUpdate, bool update);

   int m_width;
   int m_height;
   int m_length;
   bool m_sam;
   uint8_t* m_pData;
   uint8_t* m_pRGB24Data;
   uint16_t m_segData1[128];
   uint16_t m_segData2[128];
   uint8_t* m_pLevelData;
   uint32_t* m_pRGB32Data;
   uint16_t* m_pRGB565Data;
   uint8_t m_palette[192];
   AlphaNumeric* m_pAlphaNumeric;
   Serum* m_pSerum;
   ZeDMD* m_pZeDMD;
#if !((defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || defined(__ANDROID__))
   Pixelcade* m_pPixelcade;
#endif
   bool m_updated;

   std::thread* m_pThread;
   std::queue<DMDUpdate*> m_updates;
   std::mutex m_mutex;
   std::condition_variable m_condVar;
   bool m_running;

   static bool m_finding;
};

}