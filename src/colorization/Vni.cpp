#include "vni.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "DMDInternal.h"
#include "DMDUtil/Config.h"
#include "DMDUtil/Logger.h"
#include "FrameUtil.h"
#include "OutputFilters.h"

namespace DMDUtil
{

namespace
{

std::string ToLower(const std::string& value)
{
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value)
  {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

bool FindCaseInsensitiveFile(const std::string& dir, const std::string& filename, std::string* outPath)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
  {
    return false;
  }

  const std::string target = ToLower(filename);
  for (const auto& entry : fs::directory_iterator(dir, ec))
  {
    if (ec)
    {
      break;
    }
    if (!entry.is_regular_file(ec))
    {
      continue;
    }

    const std::string name = entry.path().filename().string();
    if (ToLower(name) == target)
    {
      *outPath = entry.path().string();
      return true;
    }
  }

  return false;
}

std::string BuildAltColorDir(const char* altColorPath, const char* romName)
{
  std::string path = altColorPath ? altColorPath : "";
  if (!path.empty() && path.back() != '/' && path.back() != '\\')
  {
    path += '/';
  }
  if (romName && romName[0] != '\0')
  {
    path += romName;
    path += '/';
  }
  return path;
}

}  // namespace

void DMD::VniThread()
{
#ifdef DMDUTIL_ENABLE_VNI
  Config* const pConfig = Config::GetInstance();

  if (!pConfig->IsAltColor())
  {
    return;
  }

  uint16_t bufferPosition = 0;
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};

  (void)m_stopFlag.load(std::memory_order_acquire);

  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();
  bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl,
                 [&]()
                 {
                   return m_stopFlag.load(std::memory_order_relaxed) ||
                          (m_updateBufferQueuePosition.load(std::memory_order_relaxed) != bufferPosition);
                 });
    sl.unlock();

    if (m_stopFlag.load(std::memory_order_acquire))
    {
      if (m_pVni)
      {
        Vni_Dispose(m_pVni);
        m_pVni = nullptr;
      }
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (bufferPosition != updateBufferQueuePosition)
    {
      ++bufferPosition;  // 65635 + 1 = 0
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pSerum)
      {
        if (m_pVni)
        {
          Vni_Dispose(m_pVni);
          m_pVni = nullptr;
        }
        strcpy(name, "");
        continue;
      }

      if (m_pVni && (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24 ||
                     m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16))
      {
        Vni_Dispose(m_pVni);
        m_pVni = nullptr;
        strcpy(name, "");
        QueueBuffer();
        continue;
      }

      if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data)
      {
        if (strcmp(m_romName, name) != 0)
        {
          strcpy(name, m_romName);

          if (m_pVni)
          {
            Vni_Dispose(m_pVni);
            m_pVni = nullptr;
          }

          if (m_altColorPath[0] == '\0') strcpy(m_altColorPath, Config::GetInstance()->GetAltColorPath());

          std::string baseDir = BuildAltColorDir(m_altColorPath, m_romName);
          std::string palPath;
          std::string vniPath;
          std::string pacPath;

          const std::string baseName = std::string(m_romName);
          FindCaseInsensitiveFile(baseDir, baseName + ".pal", &palPath);
          FindCaseInsensitiveFile(baseDir, baseName + ".vni", &vniPath);
          FindCaseInsensitiveFile(baseDir, baseName + ".pac", &pacPath);

          const char* vniKey = pConfig->GetVniKey();
          if (!pacPath.empty() && (!vniKey || vniKey[0] == '\0'))
          {
            Log(DMDUtil_LogLevel_ERROR, "VNI: pac file requires VNI key for %s", m_romName);
          }
          else if (!palPath.empty() || !vniPath.empty() || !pacPath.empty())
          {
            m_pVni = Vni_LoadFromPaths(
                palPath.empty() ? nullptr : palPath.c_str(), vniPath.empty() ? nullptr : vniPath.c_str(),
                pacPath.empty() ? nullptr : pacPath.c_str(), (vniKey && vniKey[0] != '\0') ? vniKey : nullptr);
            if (m_pVni)
            {
              Log(DMDUtil_LogLevel_INFO, "VNI: Loaded colorization for %s", m_romName);
            }
          }
        }

        if (m_pVni)
        {
          uint16_t width = m_pUpdateBufferQueue[bufferPositionMod]->width;
          uint16_t height = m_pUpdateBufferQueue[bufferPositionMod]->height;
          uint8_t depth = (uint8_t)m_pUpdateBufferQueue[bufferPositionMod]->depth;

          uint32_t result = Vni_Colorize(m_pVni, m_pUpdateBufferQueue[bufferPositionMod]->data, width, height, depth);
          if (result)
          {
            const Vni_Frame_Struc* frame = Vni_GetFrame(m_pVni);
            if (frame && frame->has_frame && frame->frame && frame->palette && frame->bitlen <= 8)
            {
              const size_t frameSize = (size_t)frame->width * frame->height;
              const size_t paletteSize = (size_t)1u << frame->bitlen;

              if (frameSize <= (256u * 64u) && paletteSize <= 256u)
              {
                auto vniUpdate = std::make_shared<Update>();
                vniUpdate->mode = Mode::Vni;
                vniUpdate->depth = frame->bitlen;
                vniUpdate->width = (uint16_t)frame->width;
                vniUpdate->height = (uint16_t)frame->height;
                vniUpdate->hasData = true;
                vniUpdate->hasSegData = false;
                vniUpdate->hasSegData2 = false;
                memcpy(vniUpdate->data, frame->frame, frameSize);
                memcpy(vniUpdate->segData, frame->palette, paletteSize * 3);

                uint32_t queuedTimestamp = 0;
                bool hasTimestamp = GetQueueTimestamp(bufferPositionMod, queuedTimestamp);
                QueueUpdate(vniUpdate, false, hasTimestamp, queuedTimestamp);
              }
            }
          }
          else if (showNotColorizedFrames || dumpNotColorizedFrames)
          {
            Log(DMDUtil_LogLevel_DEBUG, "VNI: unidentified frame detected");

            auto noVniUpdate = std::make_shared<Update>();
            noVniUpdate->mode = Mode::NotColorized;
            noVniUpdate->depth = m_pUpdateBufferQueue[bufferPositionMod]->depth;
            noVniUpdate->width = m_pUpdateBufferQueue[bufferPositionMod]->width;
            noVniUpdate->height = m_pUpdateBufferQueue[bufferPositionMod]->height;
            noVniUpdate->hasData = true;
            noVniUpdate->hasSegData = false;
            noVniUpdate->hasSegData2 = false;
            memcpy(noVniUpdate->data, m_pUpdateBufferQueue[bufferPositionMod]->data,
                   (size_t)m_pUpdateBufferQueue[bufferPositionMod]->width *
                       m_pUpdateBufferQueue[bufferPositionMod]->height);

            uint32_t queuedTimestamp = 0;
            bool hasTimestamp = GetQueueTimestamp(bufferPositionMod, queuedTimestamp);
            QueueUpdate(noVniUpdate, false, hasTimestamp, queuedTimestamp);
          }
        }
      }
    }
  }
#endif
}

}  // namespace DMDUtil
