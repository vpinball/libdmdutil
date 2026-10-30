#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "DMDInternal.h"
#include "DMDUtil/Config.h"
#include "DMDUtil/Logger.h"
#include "FrameUtil.h"
#include "OutputFilters.h"
#include "pupdmd.h"

namespace DMDUtil
{

void PUPDMDCALLBACK PUPDMDLogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log(DMDUtil_LogLevel_INFO, "%s", buffer);
}

void DMD::PupDMDThread()
{
  std::vector<uint8_t> renderBuffer(DMDUTIL_MAX_FRAME_PIXELS, 0);
  uint8_t palette[192] = {0};
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};

  OutputLoop(true, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               const Update& update = *m_pUpdateBufferQueue[slot];

               if (strcmp(m_romName, name) != 0)
               {
                 snprintf(name, sizeof(name), "%s", m_romName);

                 if (Config::GetInstance()->IsPUPCapture())
                 {
                   delete m_pPUPDMD;
                   m_pPUPDMD = nullptr;

                   if (name[0] != '\0')
                   {
                     if (m_pupVideosPath[0] == '\0')
                       snprintf(m_pupVideosPath, sizeof(m_pupVideosPath), "%s",
                                Config::GetInstance()->GetPUPVideosPath());
                     m_pPUPDMD = new PUPDMD::DMD();
                     m_pPUPDMD->SetLogCallback(PUPDMDLogCallback, nullptr);

                     if (!m_pPUPDMD->Load(m_pupVideosPath, m_romName, (uint8_t)update.depth))
                     {
                       delete m_pPUPDMD;
                       m_pPUPDMD = nullptr;
                     }
                   }
                 }
               }

               if (!m_pPUPDMD || !update.hasData || update.mode != Mode::Data || update.depth == 24) return;

               const uint16_t width = update.width;
               const uint16_t height = update.height;
               const size_t length = (size_t)width * height;
               if (length == 0 || length > DMDUTIL_MAX_FRAME_PIXELS) return;
               if (memcmp(renderBuffer.data(), update.data, length) == 0) return;

               memcpy(renderBuffer.data(), update.data, length);
               const uint8_t depth = (uint8_t)update.depth;

               uint8_t scaledBuffer[128 * 32];
               if (width == 128 && height == 32)
                 memcpy(scaledBuffer, renderBuffer.data(), 128 * 32);
               else if (width == 128 && height == 16)
                 FrameUtil::Helper::CenterIndexed(scaledBuffer, 128, 32, renderBuffer.data(), 128, 16);
               else if (width == 192 && height == 64)
                 FrameUtil::Helper::ScaleDownPUP(scaledBuffer, 128, 32, renderBuffer.data(), 192, 64);
               else
                 return;

               uint16_t triggerID = 0;
               if (Config::GetInstance()->IsPUPExactColorMatch())
               {
                 triggerID = m_pPUPDMD->MatchIndexed(scaledBuffer, width, height);
               }
               else
               {
                 UpdatePalette(palette, depth, 255, 69, 0);

                 std::vector<uint8_t> frame(length * 3);
                 for (size_t i = 0; i < length; i++)
                 {
                   memcpy(&frame[i * 3], &palette[scaledBuffer[i] * 3], 3);
                 }
                 triggerID = m_pPUPDMD->Match(frame.data(), width, height, false);
               }

               if (triggerID > 0) HandleTrigger(triggerID);
             });
}

}  // namespace DMDUtil
