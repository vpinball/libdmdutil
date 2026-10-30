#include "serum.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <thread>
#include <unordered_set>

#include "DMDInternal.h"
#include "DMDUtil/Config.h"
#include "DMDUtil/LevelDMD.h"
#include "DMDUtil/Logger.h"
#include "DMDUtil/RGB24DMD.h"
#include "FrameUtil.h"
#include "OutputFilters.h"
#include "TimeUtils.h"
#include "ZeDMD.h"
#include "serum-decode.h"

namespace DMDUtil
{

void SERUM_CALLBACK Serum_LogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log(DMDUtil_LogLevel_INFO, "%s", buffer);
}

void DMD::SerumThread()
{
  Config* const pConfig = Config::GetInstance();
#define SERUM_TRIGGER_MIN_EVENT 50000
#define SERUM_TRIGGER_MAX_EVENT 62000

  if (pConfig->IsAltColor())
  {
    Serum_SetLogCallback(Serum_LogCallback, nullptr);

    uint16_t bufferPosition = 0;
    uint32_t prevTriggerId = 0;
    char name[DMDUTIL_MAX_NAME_SIZE] = {0};
    char csvPath[DMDUTIL_MAX_PATH_SIZE + DMDUTIL_MAX_NAME_SIZE + DMDUTIL_MAX_NAME_SIZE + 10] = {0};
    uint32_t nextRotation = 0;
    Update* lastDmdUpdate = nullptr;
    uint8_t flags = 0;

    const auto rotateIfDue = [&](uint32_t now)
    {
      if (!m_pSerum || nextRotation == 0 || m_pSerum->rotationtimer == 0 || !lastDmdUpdate || now < nextRotation)
        return;

      uint32_t result = Serum_Rotate();

      Log(DMDUtil_LogLevel_DEBUG, "Serum: rotation=%lu, flags=%lu", m_pSerum->rotationtimer, result >> 16);

      QueueSerumFrames(lastDmdUpdate, result & 0x10000, result & 0x20000, false, 0);

      if (result > 0 && ((result & 0xffff) < 2048))
      {
        nextRotation = now + m_pSerum->rotationtimer;
      }
      else
        nextRotation = 0;
    };

    (void)m_stopFlag.load(std::memory_order_acquire);

    bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();
    bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();
    if (pConfig->IsSerumPUPTriggers()) Serum_EnablePupTrigers();

    while (true)
    {
      if (m_stopFlag.load(std::memory_order_acquire))
      {
        if (m_pSerum)
        {
          Serum_Dispose();
          m_serumHasTimestamp = false;
          m_serumLastTimestampMs = 0;
        }

        return;
      }

      {
        const auto hasWork = [&]()
        {
          return m_stopFlag.load(std::memory_order_relaxed) ||
                 (m_updateBufferQueuePosition.load(std::memory_order_relaxed) != bufferPosition);
        };
        std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
        if (nextRotation == 0)
        {
          m_dmdCV.wait(sl, hasWork);
        }
        else
        {
          // Sleep until the next rotation is due instead of spinning, but wake up for new frames.
          const int32_t untilRotation = static_cast<int32_t>(nextRotation - GetMonotonicTimeMs());
          if (untilRotation > 0) m_dmdCV.wait_for(sl, std::chrono::milliseconds(untilRotation), hasWork);
        }
        sl.unlock();
      }

      uint32_t now = GetMonotonicTimeMs();

      const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
      while (bufferPosition != updateBufferQueuePosition)
      {
        // Don't use GetNextBufferPosition() here, we need all frames for PUP triggers!
        ++bufferPosition;  // 65635 + 1 = 0
        uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumCommand)
        {
          if (m_pSerum && m_pUpdateBufferQueue[bufferPositionMod]->hasData &&
              m_pUpdateBufferQueue[bufferPositionMod]->hasSegData)
          {
            const char source = static_cast<char>(m_pUpdateBufferQueue[bufferPositionMod]->data[0]);
            const uint8_t value = m_pUpdateBufferQueue[bufferPositionMod]->data[1];
            const uint16_t event = m_pUpdateBufferQueue[bufferPositionMod]->segData[0];

            if (source == 'D' && value == 1 && event >= SERUM_TRIGGER_MIN_EVENT && event <= SERUM_TRIGGER_MAX_EVENT)
            {
              uint32_t result = Serum_Scene_Trigger(event);

              if (result != IDENTIFY_NO_FRAME && result != IDENTIFY_SAME_FRAME && lastDmdUpdate)
              {
                QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES, false,
                                 0);
              }

              if (result > 0 && ((result & 0xffff) < 2048))
              {
                nextRotation = now + m_pSerum->rotationtimer;
                if (result & 0x40000)
                  Log(DMDUtil_LogLevel_DEBUG, "Serum: starting scene rotation, timer=%lu", m_pSerum->rotationtimer);
              }
              else
                nextRotation = 0;
            }
          }
          continue;
        }

        if (m_pSerum && (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24 ||
                         m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16))
        {
          // DMDServer accepted a different connection, turn off Serum Colorization.
          Serum_Dispose();
          m_pSerum = nullptr;
          m_serumHasTimestamp = false;
          m_serumLastTimestampMs = 0;
          lastDmdUpdate = nullptr;
          strcpy(name, "");
          QueueBuffer();
          continue;
        }

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data)
        {
          if (strcmp(m_romName, name) != 0)
          {
            // don't load Serum until all displays are found
            if (m_finding.load(std::memory_order_acquire))
            {
              QueueBuffer();
              continue;
            }

            strcpy(name, m_romName);

            if (m_pSerum)
            {
              Serum_Dispose();
              m_pSerum = nullptr;
              m_serumHasTimestamp = false;
              m_serumLastTimestampMs = 0;
              lastDmdUpdate = nullptr;
            }

            if (m_altColorPath[0] == '\0') strcpy(m_altColorPath, Config::GetInstance()->GetAltColorPath());
            flags = 0;
            const bool zedmdOnly = m_pZeDMD && m_rgb24DMDs.empty() && m_levelDMDs.empty() && !m_pDMDExtStream &&
                                   !m_pPixelcadeDMD && !m_pPinDMD3 && !m_PIN2DMDConnected;

            if (zedmdOnly)
            {
              // A single ZeDMD only needs the frame height it can actually render.
              flags = (m_pZeDMD->GetHeight() == 64) ? FLAG_REQUEST_64P_FRAMES : FLAG_REQUEST_32P_FRAMES;
            }

            // At the moment, ZeDMD HD, PIN2DMD HD and RGB24DMD are the only devices supporting 64P frames.
            // Not requesting 64P saves memory.
            if (!zedmdOnly && m_pZeDMD)
            {
              if (m_pZeDMD->GetHeight() == 64)
                flags |= FLAG_REQUEST_64P_FRAMES;
              else
                flags |= FLAG_REQUEST_32P_FRAMES;
            }

            if (m_rgb24DMDs.size() > 0)
            {
              for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
              {
                if (pRGB24DMD->GetHeight() == 64)
                  flags |= FLAG_REQUEST_64P_FRAMES;
                else
                  flags |= FLAG_REQUEST_32P_FRAMES;
              }
            }

            if (m_levelDMDs.size() > 0)
            {
              for (LevelDMD* pLevelDMD : m_levelDMDs)
              {
                if (pLevelDMD->GetHeight() == 64)
                  flags |= FLAG_REQUEST_64P_FRAMES;
                else
                  flags |= FLAG_REQUEST_32P_FRAMES;
              }
            }

            if (m_pPixelcadeDMD) flags |= FLAG_REQUEST_32P_FRAMES;
            if (m_pPinDMD3) flags |= FLAG_REQUEST_32P_FRAMES;

            if (m_PIN2DMDConnected)
            {
              if (m_PIN2DMDHeight == 64)
                flags |= FLAG_REQUEST_64P_FRAMES;
              else
                flags |= FLAG_REQUEST_32P_FRAMES;
            }

            if (m_pDMDExtStream) flags |= FLAG_REQUEST_32P_FRAMES;

            if (!flags) flags |= FLAG_REQUEST_32P_FRAMES;
            flags |= FLAG_REQUEST_FALLBACK;

            m_pSerum = (name[0] != '\0') ? Serum_Load(m_altColorPath, m_romName, flags) : nullptr;
            if (m_pSerum)
            {
              Log(DMDUtil_LogLevel_INFO, "Serum: Loaded v%d colorization for %s", m_pSerum->SerumVersion, m_romName);

              Serum_SetIgnoreUnknownFramesTimeout(Config::GetInstance()->GetIgnoreUnknownFramesTimeout());
              Serum_SetMaximumUnknownFramesToSkip(Config::GetInstance()->GetMaximumUnknownFramesToSkip());
              m_serumHasTimestamp = false;
              m_serumLastTimestampMs = 0;
            }
          }

          if (m_pSerum)
          {
            // Serum_Colorize() restarts the timer of a rotation step that is already due when the same frame is
            // identified again, e.g. a blinking word in a dynamic zone. Run the due step first, otherwise the
            // rotation stutters whenever a frame arrives just after the step's time.
            rotateIfDue(GetMonotonicTimeMs());

            FrameContext frameContext{};
            GetQueueFrameContext(bufferPositionMod, frameContext);

            const auto colorizeStart = std::chrono::steady_clock::now();
            uint32_t result = Serum_Colorize(m_pUpdateBufferQueue[bufferPositionMod]->data);
            const uint32_t colorizeTimeUs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - colorizeStart)
                    .count());
            uint32_t averageColorizeTimeUs = 0;
            if (m_serumColorizeCount < (std::numeric_limits<uint64_t>::max)())
            {
              m_serumColorizeTimeTotalUs += colorizeTimeUs;
              ++m_serumColorizeCount;
              averageColorizeTimeUs = static_cast<uint32_t>(m_serumColorizeTimeTotalUs / m_serumColorizeCount);
            }

            Serum_Runtime_Metadata runtimeMetadata{};
            runtimeMetadata.size = sizeof(runtimeMetadata);
            Serum_GetRuntimeMetadata(&runtimeMetadata);

            if (result != IDENTIFY_NO_FRAME && result != IDENTIFY_SAME_FRAME)
            {
              // Log(DMDUtil_LogLevel_DEBUG, "Serum: frameID=%lu, rotation=%lu, flags=%lu", m_pSerum->frameID,
              // m_pSerum->rotationtimer, m_pSerum->flags);

              lastDmdUpdate = m_pUpdateBufferQueue[bufferPositionMod];

              uint32_t queuedTimestamp = 0;
              bool hasTimestamp = GetQueueTimestamp(bufferPositionMod, queuedTimestamp);

              std::shared_ptr<Update> primaryOutput;
              QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES,
                               hasTimestamp, queuedTimestamp, &primaryOutput);
              RecordSerumColorizeCapture(frameContext, primaryOutput, hasTimestamp, queuedTimestamp, false, result,
                                         runtimeMetadata.serumVersion, runtimeMetadata.frameID,
                                         runtimeMetadata.triggerID, runtimeMetadata.rotationtimer,
                                         runtimeMetadata.featureFlags, colorizeTimeUs, averageColorizeTimeUs);

              if (result > 0 && ((result & 0xffff) < 2048))
              {
                nextRotation = now + m_pSerum->rotationtimer;
                if (result & 0x40000)
                  Log(DMDUtil_LogLevel_DEBUG, "Serum: starting scene rotation, timer=%lu", m_pSerum->rotationtimer);
              }
              else
                nextRotation = 0;

              if (m_pSerum->triggerID < 0xffffffff)
              {
                HandleTrigger(m_pSerum->triggerID);
                prevTriggerId = m_pSerum->triggerID;
              }
            }
            else if (showNotColorizedFrames || dumpNotColorizedFrames)
            {
              Log(DMDUtil_LogLevel_DEBUG, "Serum: unidentified frame detected");

              auto noSerumUpdate = std::make_shared<Update>();
              noSerumUpdate->mode = Mode::NotColorized;
              noSerumUpdate->depth = m_pUpdateBufferQueue[bufferPositionMod]->depth;
              noSerumUpdate->width = m_pUpdateBufferQueue[bufferPositionMod]->width;
              noSerumUpdate->height = m_pUpdateBufferQueue[bufferPositionMod]->height;
              noSerumUpdate->hasData = true;
              noSerumUpdate->hasSegData = false;
              noSerumUpdate->hasSegData2 = false;
              memcpy(noSerumUpdate->data, m_pUpdateBufferQueue[bufferPositionMod]->data,
                     (size_t)m_pUpdateBufferQueue[bufferPositionMod]->width *
                         m_pUpdateBufferQueue[bufferPositionMod]->height);

              uint32_t queuedTimestamp = 0;
              bool hasTimestamp = GetQueueTimestamp(bufferPositionMod, queuedTimestamp);
              QueueUpdate(noSerumUpdate, false, hasTimestamp, queuedTimestamp);
              RecordSerumColorizeCapture(frameContext, std::shared_ptr<Update>(), hasTimestamp, queuedTimestamp, false,
                                         result, runtimeMetadata.serumVersion, runtimeMetadata.frameID,
                                         runtimeMetadata.triggerID, runtimeMetadata.rotationtimer,
                                         runtimeMetadata.featureFlags, colorizeTimeUs, averageColorizeTimeUs);
            }
            else
            {
              RecordSerumColorizeCapture(frameContext, std::shared_ptr<Update>(), false, 0, false, result,
                                         runtimeMetadata.serumVersion, runtimeMetadata.frameID,
                                         runtimeMetadata.triggerID, runtimeMetadata.rotationtimer,
                                         runtimeMetadata.featureFlags, colorizeTimeUs, averageColorizeTimeUs);
            }
          }
        }
      }

      now = GetMonotonicTimeMs();
      rotateIfDue(now);

      // A due rotation that can't run anymore would otherwise keep the wait above from sleeping.
      if (nextRotation > 0 && now >= nextRotation && (!m_pSerum || m_pSerum->rotationtimer == 0 || !lastDmdUpdate))
        nextRotation = 0;
    }
  }
}

void DMD::QueueSerumFrames(Update* dmdUpdate, bool render32, bool render64, bool hasTimestamp, uint32_t timestampMs,
                           std::shared_ptr<Update>* primaryOutput)
{
  if (!render32 && !render64) return;

  if (!hasTimestamp && m_serumHasTimestamp && m_pSerum && m_pSerum->rotationtimer > 0)
  {
    timestampMs = m_serumLastTimestampMs + (uint32_t)m_pSerum->rotationtimer;
    hasTimestamp = true;
  }

  if (hasTimestamp)
  {
    m_serumHasTimestamp = true;
    m_serumLastTimestampMs = timestampMs;
  }

  auto serumUpdate = std::make_shared<Update>();
  serumUpdate->hasData = true;
  serumUpdate->hasSegData = false;
  serumUpdate->hasSegData2 = false;

  if (m_pSerum->SerumVersion == SERUM_V1 && render32)
  {
    const size_t frameBytes = (size_t)dmdUpdate->width * dmdUpdate->height;
    if (frameBytes == 0 || frameBytes > sizeof(serumUpdate->data))
    {
      Log(DMDUtil_LogLevel_ERROR, "Serum: Invalid v1 frame size %ux%u, skipping frame", dmdUpdate->width,
          dmdUpdate->height);
      return;
    }

    serumUpdate->mode = Mode::SerumV1;
    serumUpdate->depth = 6;
    serumUpdate->width = dmdUpdate->width;
    serumUpdate->height = dmdUpdate->height;
    memcpy(serumUpdate->data, m_pSerum->frame, frameBytes);
    memcpy(serumUpdate->segData, m_pSerum->palette, PALETTE_SIZE);

    if (primaryOutput)
    {
      *primaryOutput = serumUpdate;
      primaryOutput = nullptr;
    }
    QueueUpdate(serumUpdate, false, hasTimestamp, timestampMs);
  }
  else if (m_pSerum->SerumVersion == SERUM_V2)
  {
    if (m_pSerum->width32 > 0 && m_pSerum->width64 == 0)
    {
      if (render32 || render64)
      {
        const size_t frameWords32 = (size_t)m_pSerum->width32 * 32u;
        if (frameWords32 > (sizeof(serumUpdate->segData) / sizeof(serumUpdate->segData[0])))
        {
          Log(DMDUtil_LogLevel_ERROR, "Serum: Invalid v2 32p frame width %u, skipping frame", m_pSerum->width32);
          return;
        }

        serumUpdate->mode = Mode::SerumV2_32;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width32;
        serumUpdate->height = 32;
        memcpy(serumUpdate->segData, m_pSerum->frame32, frameWords32 * sizeof(uint16_t));

        if (primaryOutput)
        {
          *primaryOutput = serumUpdate;
          primaryOutput = nullptr;
        }
        QueueUpdate(serumUpdate, false, hasTimestamp, timestampMs);
      }
    }
    else if (m_pSerum->width32 == 0 && m_pSerum->width64 > 0)
    {
      if (render64)
      {
        const size_t frameWords64 = (size_t)m_pSerum->width64 * 64u;
        if (frameWords64 > (sizeof(serumUpdate->segData) / sizeof(serumUpdate->segData[0])))
        {
          Log(DMDUtil_LogLevel_ERROR, "Serum: Invalid v2 64p frame width %u, skipping frame", m_pSerum->width64);
          return;
        }

        serumUpdate->mode = Mode::SerumV2_64;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width64;
        serumUpdate->height = 64;
        memcpy(serumUpdate->segData, m_pSerum->frame64, frameWords64 * sizeof(uint16_t));

        if (primaryOutput)
        {
          *primaryOutput = serumUpdate;
          primaryOutput = nullptr;
        }
        QueueUpdate(serumUpdate, false, hasTimestamp, timestampMs);
      }
    }
    else if (m_pSerum->width32 > 0 && m_pSerum->width64 > 0)
    {
      if (render32)
      {
        const size_t frameWords32 = (size_t)m_pSerum->width32 * 32u;
        if (frameWords32 > (sizeof(serumUpdate->segData) / sizeof(serumUpdate->segData[0])))
        {
          Log(DMDUtil_LogLevel_ERROR, "Serum: Invalid v2 32p frame width %u, skipping frame", m_pSerum->width32);
          return;
        }

        serumUpdate->mode = Mode::SerumV2_32_64;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width32;
        serumUpdate->height = 32;
        memcpy(serumUpdate->segData, m_pSerum->frame32, frameWords32 * sizeof(uint16_t));

        if (primaryOutput)
        {
          *primaryOutput = serumUpdate;
          primaryOutput = nullptr;
        }
        QueueUpdate(serumUpdate, false, hasTimestamp, timestampMs);
      }

      if (render64)
      {
        const size_t frameWords64 = (size_t)m_pSerum->width64 * 64u;
        if (frameWords64 > (sizeof(serumUpdate->segData) / sizeof(serumUpdate->segData[0])))
        {
          Log(DMDUtil_LogLevel_ERROR, "Serum: Invalid v2 64p frame width %u, skipping frame", m_pSerum->width64);
          return;
        }

        // We can't reuse the shared pointer from above because it might have been sent already.
        auto serumUpdateHD = std::make_shared<Update>();
        serumUpdateHD->hasData = true;
        serumUpdateHD->hasSegData = false;
        serumUpdateHD->hasSegData2 = false;
        serumUpdateHD->mode = Mode::SerumV2_64_32;
        serumUpdateHD->depth = 24;
        serumUpdateHD->width = m_pSerum->width64;
        serumUpdateHD->height = 64;
        memcpy(serumUpdateHD->segData, m_pSerum->frame64, frameWords64 * sizeof(uint16_t));

        if (primaryOutput)
        {
          *primaryOutput = serumUpdateHD;
          primaryOutput = nullptr;
        }
        QueueUpdate(serumUpdateHD, false, hasTimestamp, timestampMs);
      }
    }
  }
}

void DMD::RecordSerumColorizeCapture(const FrameContext& frameContext, const std::shared_ptr<Update>& primaryOutput,
                                     bool hasTimestamp, uint32_t outputTimestampMs, bool isRotation,
                                     uint32_t serumResult, uint32_t serumVersion, uint32_t serumFrameId,
                                     uint32_t serumTriggerId, uint32_t serumRotationTimer, uint32_t serumFeatureFlags,
                                     uint32_t colorizeTimeUs, uint32_t averageColorizeTimeUs)
{
  if (!frameContext.valid)
  {
    return;
  }

  SerumCapture capture;
  capture.valid = true;
  capture.hasOutput = (primaryOutput != nullptr);
  capture.isRotation = isRotation;
  capture.hasTimestamp = hasTimestamp;
  capture.sourceOrdinal = frameContext.sourceOrdinal;
  capture.sourceFrameIndex = frameContext.sourceFrameIndex;
  capture.originalFrameIndex = frameContext.originalFrameIndex;
  capture.inputCrc32 = frameContext.inputCrc32;
  capture.inputTimestampMs = frameContext.inputTimestampMs;
  capture.inputDurationMs = frameContext.inputDurationMs;
  capture.serumResult = serumResult;
  capture.serumVersion = serumVersion;
  capture.serumFrameId = serumFrameId;
  capture.serumTriggerId = serumTriggerId;
  capture.serumRotationTimer = serumRotationTimer;
  capture.serumFeatureFlags = serumFeatureFlags;
  capture.colorizeTimeUs = colorizeTimeUs;
  capture.averageColorizeTimeUs = averageColorizeTimeUs;
  capture.outputTimestampMs = outputTimestampMs;
  if (primaryOutput)
  {
    capture.update = *primaryOutput;
  }

  std::lock_guard<std::mutex> lock(m_serumCaptureMutex);
  m_serumColorizeCaptures[frameContext.sourceOrdinal] = std::move(capture);
  while (m_serumColorizeCaptures.size() > 64)
  {
    m_serumColorizeCaptures.erase(m_serumColorizeCaptures.begin());
  }
  m_serumCaptureCv.notify_all();
}

}  // namespace DMDUtil
