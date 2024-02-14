#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/LevelDMD.h"
#include "DMDUtil/RGB24DMD.h"

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
#include "PixelcadeDMD.h"
#endif

#include <algorithm>
#include <cstring>

#include "AlphaNumeric.h"
#include "FrameUtil.h"
#include "Logger.h"
#include "Serum.h"
#include "ZeDMD.h"

namespace DMDUtil
{

void ZEDMDCALLBACK ZeDMDLogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log("%s", buffer);
}

bool DMD::m_finding = false;

DMD::DMD()
{
  for (uint8_t i = 0; i < DMDUTIL_FRAME_BUFFER_SIZE; i++)
  {
    m_updateBuffer[i] = new DMDUpdate();
  }
  m_pAlphaNumeric = new AlphaNumeric();
  m_pSerum = nullptr;
  m_pZeDMD = nullptr;
  m_pZeDMDThread = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  m_pPixelcadeDMD = nullptr;
  m_pPixelcadeDMDThread = nullptr;
#endif

  FindDevices();

  // todo virtual dmdm thread

  m_pdmdFrameReadyResetThread = new std::thread(&DMD::DmdFrameReadyResetThread, this);
}

DMD::~DMD()
{
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  m_stopFlag = true;
  ul.unlock();
  m_dmdCV.notify_all();

  m_pdmdFrameReadyResetThread->join();
  delete m_pdmdFrameReadyResetThread;
  m_pdmdFrameReadyResetThread = nullptr;

  if (m_pLevelDMDThread)
  {
    m_pLevelDMDThread->join();
    delete m_pLevelDMDThread;
    m_pLevelDMDThread = nullptr;
  }

  if (m_pRGB24DMDThread)
  {
    m_pRGB24DMDThread->join();
    delete m_pRGB24DMDThread;
    m_pRGB24DMDThread = nullptr;
  }

  if (m_pZeDMDThread)
  {
    m_pZeDMDThread->join();
    delete m_pZeDMDThread;
    m_pZeDMDThread = nullptr;
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMDThread)
  {
    m_pPixelcadeDMDThread->join();
    delete m_pPixelcadeDMDThread;
    m_pPixelcadeDMDThread = nullptr;
  }
#endif
  delete m_pAlphaNumeric;
  delete m_pSerum;
  delete m_pZeDMD;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  delete m_pPixelcadeDMD;
#endif

  for (LevelDMD* pLevelDMD : m_levelDMDs) delete pLevelDMD;
  for (RGB24DMD* pRGB24DMD : m_rgb24DMDs) delete pRGB24DMD;
}

bool DMD::IsFinding() { return m_finding; }

bool DMD::HasDisplay() const
{
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  return (m_pZeDMD != nullptr) || (m_pPixelcadeDMD != nullptr);
#else
  return (m_pZeDMD != nullptr);
#endif
}

LevelDMD* DMD::CreateLevelDMD(uint16_t width, uint16_t height, bool sam)
{
  LevelDMD* const pLevelDMD = new LevelDMD(width, height, sam);
  m_levelDMDs.push_back(pLevelDMD);
  if (!m_pLevelDMDThread) m_pLevelDMDThread = new std::thread(&DMD::LevelDMDThread, this);
  return pLevelDMD;
}

bool DMD::DestroyLevelDMD(LevelDMD* pLevelDMD)
{
  auto it = std::find(m_levelDMDs.begin(), m_levelDMDs.end(), pLevelDMD);
  if (it != m_levelDMDs.end())
  {
    m_levelDMDs.erase(it);
    delete pLevelDMD;

    if (m_levelDMDs.empty())
    {
      //@todo terminate LevelDMDThread
    }

    return true;
  }
  return false;
}

RGB24DMD* DMD::CreateRGB24DMD(uint16_t width, uint16_t height)
{
  RGB24DMD* const pRGB24DMD = new RGB24DMD(width, height);
  m_rgb24DMDs.push_back(pRGB24DMD);
  if (!m_pRGB24DMDThread) m_pRGB24DMDThread = new std::thread(&DMD::RGB24DMDThread, this);
  return pRGB24DMD;
}

bool DMD::DestroyRGB24DMD(RGB24DMD* pRGB24DMD)
{
  auto it = std::find(m_rgb24DMDs.begin(), m_rgb24DMDs.end(), pRGB24DMD);
  if (it != m_rgb24DMDs.end())
  {
    m_rgb24DMDs.erase(it);
    delete pRGB24DMD;

    if (m_rgb24DMDs.empty())
    {
      //@todo terminate RGB24DMDThread
    }

    return true;
  }
  return false;
}

void DMD::UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                     DMDMode mode, const char* name)
{
  DMDUpdate dmdUpdate = DMDUpdate();
  dmdUpdate.mode = mode;
  dmdUpdate.depth = depth;
  dmdUpdate.width = width;
  dmdUpdate.height = height;
  if (pData)
  {
    memcpy(dmdUpdate.data, pData, width * height * (mode == DMDMode::RGB24 ? 3 : 1));
    dmdUpdate.hasData = true;
  }
  else
  {
    dmdUpdate.hasData = false;
  }
  dmdUpdate.hasSegData = false;
  dmdUpdate.hasSegData2 = false;
  dmdUpdate.r = r;
  dmdUpdate.g = g;
  dmdUpdate.b = b;
  strcpy(dmdUpdate.name, name ? name : "");

  new std::thread(
      [this, dmdUpdate]()
      {
        std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);

        memcpy(m_updateBuffer[m_updateBufferPosition], &dmdUpdate, sizeof(DMDUpdate));

        m_dmdFrameReady = true;
        if (++m_updateBufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) m_updateBufferPosition = 0;
        ul.unlock();
        m_dmdCV.notify_all();
      });
}

void DMD::UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                     const char* name)
{
  UpdateData(pData, depth, width, height, r, g, b, DMDMode::Data, name);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                          uint8_t b)
{
  UpdateData(pData, depth, width, height, r, g, b, DMDMode::RGB24, nullptr);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b)
{
  UpdateData(pData, 24, width, height, r, g, b, DMDMode::RGB24, nullptr);
}

void DMD::UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                                 uint8_t g, uint8_t b, const char* name)
{
  DMDUpdate dmdUpdate = DMDUpdate();
  dmdUpdate.mode = DMDMode::AlphaNumeric;
  dmdUpdate.layout = layout;
  dmdUpdate.depth = 2;
  dmdUpdate.width = 128;
  dmdUpdate.height = 32;
  dmdUpdate.hasData = false;
  if (pData1)
  {
    memcpy(dmdUpdate.segData, pData1, 128 * sizeof(uint16_t));
    dmdUpdate.hasSegData = true;
  }
  else
  {
    dmdUpdate.hasSegData = false;
  }
  if (pData2)
  {
    memcpy(dmdUpdate.segData2, pData2, 128 * sizeof(uint16_t));
    dmdUpdate.hasSegData2 = true;
  }
  else
  {
    dmdUpdate.hasSegData2 = false;
  }
  dmdUpdate.r = r;
  dmdUpdate.g = g;
  dmdUpdate.b = b;
  strcpy(dmdUpdate.name, name ? name : "");

  new std::thread(
      [this, dmdUpdate]()
      {
        std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);

        memcpy(m_updateBuffer[m_updateBufferPosition], &dmdUpdate, sizeof(DMDUpdate));

        m_dmdFrameReady = true;
        if (++m_updateBufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) m_updateBufferPosition = 0;
        ul.unlock();
        m_dmdCV.notify_all();
      });
}

void DMD::FindDevices()
{
  if (m_finding) return;

  m_finding = true;

  new std::thread(
      [this]()
      {
        Config* const pConfig = Config::GetInstance();

        ZeDMD* pZeDMD = nullptr;

        if (pConfig->IsZeDMD())
        {
          pZeDMD = new ZeDMD();
          pZeDMD->SetLogCallback(ZeDMDLogCallback, nullptr);

          if (pConfig->GetZeDMDDevice() != nullptr && pConfig->GetZeDMDDevice()[0] != '\0')
            pZeDMD->SetDevice(pConfig->GetZeDMDDevice());

          if (pZeDMD->Open())
          {
            if (pConfig->IsZeDMDDebug()) pZeDMD->EnableDebug();

            if (pConfig->GetZeDMDRGBOrder() != -1) pZeDMD->SetRGBOrder(pConfig->GetZeDMDRGBOrder());

            if (pConfig->GetZeDMDBrightness() != -1) pZeDMD->SetBrightness(pConfig->GetZeDMDBrightness());

            if (pConfig->IsZeDMDSaveSettings()) pZeDMD->SaveSettings();

            m_pZeDMDThread = new std::thread(&DMD::ZeDMDThread, this);
          }
          else
          {
            delete pZeDMD;
            pZeDMD = nullptr;
          }
        }

        m_pZeDMD = pZeDMD;

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
        PixelcadeDMD* pPixelcadeDMD = nullptr;

        if (pConfig->IsPixelcade())
        {
          pPixelcadeDMD = PixelcadeDMD::Connect(pConfig->GetPixelcadeDevice(), 128, 32);
          m_pPixelcadeDMDThread = new std::thread(&DMD::PixelcadeDMDThread, this);
        }

        m_pPixelcadeDMD = pPixelcadeDMD;
#endif

        m_finding = false;
      });
}

void DMD::DmdFrameReadyResetThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = "";

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();

    if (strcmp(m_updateBuffer[m_updateBufferPosition]->name, name) != 0)
    {
      if (m_pSerum)
      {
        delete (m_pSerum);
        m_pSerum = nullptr;
      }

      strcpy(name, m_updateBuffer[m_updateBufferPosition]->name);

      m_pSerum = (Config::GetInstance()->IsAltColor() && name[0] != '\0') ? Serum::Load(name) : nullptr;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
    m_dmdFrameReady = false;
    ul.unlock();

    if (m_stopFlag)
    {
      return;
    }
  }
}

void DMD::ZeDMDThread()
{
  int bufferPosition = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t segData1[128];
  uint16_t segData2[128];
  uint8_t palette[192] = {0};
  // ZeDMD HD supports 256 * 64 pixels.
  uint8_t renderBuffer[256 * 64] = {0};
  uint8_t rgb24Data[256 * 64 * 3] = {0};

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag)
    {
      return;
    }

    while (!m_stopFlag && bufferPosition != m_updateBufferPosition)
    {
      // Note: libzedmd has its own update detection.

      if (m_updateBuffer[bufferPosition]->hasData || m_updateBuffer[bufferPosition]->hasSegData)
      {
        if (m_updateBuffer[bufferPosition]->width != width || m_updateBuffer[bufferPosition]->height != height)
        {
          width = m_updateBuffer[bufferPosition]->width;
          height = m_updateBuffer[bufferPosition]->height;
          // Activate the correct scaling mode.
          m_pZeDMD->SetFrameSize(width, height);
        }

        bool update = false;
        if (m_updateBuffer[bufferPosition]->depth != 24)
        {
          update = UpdatePalette(palette, m_updateBuffer[bufferPosition]->depth, m_updateBuffer[bufferPosition]->r,
                                 m_updateBuffer[bufferPosition]->g, m_updateBuffer[bufferPosition]->b);
        }

        if (m_updateBuffer[bufferPosition]->mode == DMDMode::RGB24)
        {
          AdjustRGB24Depth(m_updateBuffer[bufferPosition]->data, rgb24Data, width * height * 3, palette,
                           m_updateBuffer[bufferPosition]->depth);
          m_pZeDMD->RenderRgb24(rgb24Data);
        }
        else
        {
          if (m_updateBuffer[bufferPosition]->mode == DMDMode::Data)
          {
            memcpy(renderBuffer, m_updateBuffer[bufferPosition]->data, width * height);

            if (m_pSerum)
            {
              uint8_t rotations[24] = {0};
              uint32_t triggerID;
              uint32_t hashcode;
              int frameID;

              m_pSerum->SetStandardPalette(palette, m_updateBuffer[bufferPosition]->depth);

              if (m_pSerum->ColorizeWithMetadata(renderBuffer, width, height, palette, rotations, &triggerID, &hashcode,
                                                 &frameID))
              {
                m_pZeDMD->RenderColoredGray6(renderBuffer, palette, rotations);

                // @todo: send DMD PUP Event with triggerID
              }
            }
            else
            {
              m_pZeDMD->SetPalette(palette, m_updateBuffer[bufferPosition]->depth == 2 ? 4 : 16);

              switch (m_updateBuffer[bufferPosition]->depth)
              {
                case 2:
                  m_pZeDMD->RenderGray2(renderBuffer);
                  break;

                case 4:
                  m_pZeDMD->RenderGray4(renderBuffer);
                  break;

                default:
                  //@todo log error
                  break;
              }
            }
          }
          else if (m_updateBuffer[bufferPosition]->mode == DMDMode::AlphaNumeric)
          {
            if (memcmp(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t)) != 0)
            {
              memcpy(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t));
              update = true;
            }

            if (m_updateBuffer[bufferPosition]->hasSegData2 &&
                memcmp(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t)) != 0)
            {
              memcpy(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t));
              update = true;
            }

            if (update)
            {
              if (m_updateBuffer[bufferPosition]->hasSegData2)
                m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1, segData2);
              else
                m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1);

              m_pZeDMD->SetPalette(palette, 4);
              m_pZeDMD->RenderGray2(renderBuffer);
            }
          }
        }
      }

      if (++bufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) bufferPosition = 0;
    }
  }
}

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))

void DMD::PixelcadeDMDThread()
{
  int bufferPosition = 0;
  uint16_t segData1[128];
  uint16_t segData2[128];
  uint8_t palette[192] = {0};
  uint16_t rgb565Data[128 * 32] = {0};
  uint8_t renderBuffer[128 * 32] = {0};
  uint8_t rgb24Data[128 * 32 * 3] = {0};

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag)
    {
      return;
    }

    while (!m_stopFlag && bufferPosition != m_updateBufferPosition)
    {
      // @todo scaling
      if (m_updateBuffer[bufferPosition]->width == 128 && m_updateBuffer[bufferPosition]->width == 32 &&
          (m_updateBuffer[bufferPosition]->hasData || m_updateBuffer[bufferPosition]->hasSegData))
      {
        int length = m_updateBuffer[bufferPosition]->width * m_updateBuffer[bufferPosition]->height;
        bool update = false;
        if (m_updateBuffer[bufferPosition]->depth != 24)
        {
          update = UpdatePalette(palette, m_updateBuffer[bufferPosition]->depth, m_updateBuffer[bufferPosition]->r,
                                 m_updateBuffer[bufferPosition]->g, m_updateBuffer[bufferPosition]->b);
        }

        if (m_updateBuffer[bufferPosition]->mode == DMDMode::RGB24)
        {
          AdjustRGB24Depth(m_updateBuffer[bufferPosition]->data, rgb24Data, length * 3, palette,
                           m_updateBuffer[bufferPosition]->depth);
          for (int i = 0; i < length; i++)
          {
            int pos = i * 3;
            uint32_t r = m_updateBuffer[bufferPosition]->data[pos];
            uint32_t g = m_updateBuffer[bufferPosition]->data[pos + 1];
            uint32_t b = m_updateBuffer[bufferPosition]->data[pos + 2];

            rgb565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
          }
          update = true;
        }
        else
        {
          if (m_updateBuffer[bufferPosition]->mode == DMDMode::Data)
          {
            // @todo At the momeent libserum only supports on instance. So don't apply colorization if a ZeDMD is
            // attached.
            if (m_pSerum && !m_pZeDMD)
            {
              update = m_pSerum->Convert((uint8_t*)m_updateBuffer[bufferPosition]->data, renderBuffer, palette,
                                         m_updateBuffer[bufferPosition]->width, m_updateBuffer[bufferPosition]->height);
            }
            else
            {
              memcpy(renderBuffer, (uint8_t*)m_updateBuffer[bufferPosition]->data,
                     m_updateBuffer[bufferPosition]->width * m_updateBuffer[bufferPosition]->height);
              update = true;
            }
          }
          else if (m_updateBuffer[bufferPosition]->mode == DMDMode::AlphaNumeric)
          {
            if (memcmp(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t)) != 0)
            {
              memcpy(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t));
              update = true;
            }

            if (m_updateBuffer[bufferPosition]->hasSegData2 &&
                memcmp(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t)) != 0)
            {
              memcpy(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t));
              update = true;
            }

            if (update)
            {
              uint8_t* pData;

              if (m_updateBuffer[bufferPosition]->hasSegData2)
                m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1, segData2);
              else
                m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1);
            }
          }

          if (update)
          {
            for (int i = 0; i < length; i++)
            {
              int pos = renderBuffer[i] * 3;
              uint32_t r = palette[pos];
              uint32_t g = palette[pos + 1];
              uint32_t b = palette[pos + 2];

              rgb565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
            }
          }
        }

        if (update) m_pPixelcadeDMD->Update(rgb565Data);
      }

      if (++bufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) bufferPosition = 0;
    }
  }
}
#endif

void DMD::LevelDMDThread()
{
  int bufferPosition = 0;
  uint8_t renderBuffer[256 * 64] = {0};

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag)
    {
      return;
    }

    while (!m_stopFlag && bufferPosition != m_updateBufferPosition)
    {
      if (!m_levelDMDs.empty() && m_updateBuffer[bufferPosition]->mode == DMDMode::Data && !m_pSerum &&
          m_updateBuffer[bufferPosition]->hasData)
      {
        int length = m_updateBuffer[bufferPosition]->width * m_updateBuffer[bufferPosition]->height;
        if (memcmp(renderBuffer, m_updateBuffer[bufferPosition]->data, length) != 0)
        {
          memcpy(renderBuffer, m_updateBuffer[bufferPosition]->data, length);
          for (LevelDMD* pLevelDMD : m_levelDMDs)
          {
            if (pLevelDMD->GetLength() == length * 3)
              pLevelDMD->Update(renderBuffer, m_updateBuffer[bufferPosition]->depth);
          }
        }
      }

      if (++bufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) bufferPosition = 0;
    }
  }
}

void DMD::RGB24DMDThread()
{
  int bufferPosition = 0;
  uint16_t segData1[128];
  uint16_t segData2[128];
  uint8_t palette[192] = {0};
  uint8_t renderBuffer[256 * 64] = {0};
  uint8_t rgb24Data[256 * 64 * 3] = {0};

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag)
    {
      return;
    }

    while (!m_stopFlag && bufferPosition != m_updateBufferPosition)
    {
      if (!m_rgb24DMDs.empty() &&
          (m_updateBuffer[bufferPosition]->hasData || m_updateBuffer[bufferPosition]->hasSegData))
      {
        int length = m_updateBuffer[bufferPosition]->width * m_updateBuffer[bufferPosition]->height;
        bool update = false;

        if (m_updateBuffer[bufferPosition]->mode == DMDMode::RGB24)
        {
          if (memcmp(rgb24Data, m_updateBuffer[bufferPosition]->data, length * 3) != 0)
          {
            if (m_updateBuffer[bufferPosition]->depth != 24)
            {
              UpdatePalette(palette, m_updateBuffer[bufferPosition]->depth, m_updateBuffer[bufferPosition]->r,
                            m_updateBuffer[bufferPosition]->g, m_updateBuffer[bufferPosition]->b);
            }

            AdjustRGB24Depth(m_updateBuffer[bufferPosition]->data, rgb24Data, length * 3, palette,
                             m_updateBuffer[bufferPosition]->depth);

            for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
            {
              if (pRGB24DMD->GetLength() == length * 3) pRGB24DMD->Update(rgb24Data);
            }
            // Reset renderBuffer in case the mode changes for the next frame to ensure that memcmp() will detect it.
            memset(renderBuffer, 0, sizeof(renderBuffer));
          }
        }
        else
        {
          // @todo At the momeent libserum only supports on instance. So don't apply colorization if any hardware DMD is
          // attached.
          if (m_updateBuffer[bufferPosition]->mode == DMDMode::Data && m_pSerum && !HasDisplay())
          {
            update = m_pSerum->Convert(m_updateBuffer[bufferPosition]->data, renderBuffer, palette,
                                       m_updateBuffer[bufferPosition]->width, m_updateBuffer[bufferPosition]->height);
          }
          else
          {
            update = UpdatePalette(palette, m_updateBuffer[bufferPosition]->depth, m_updateBuffer[bufferPosition]->r,
                                   m_updateBuffer[bufferPosition]->g, m_updateBuffer[bufferPosition]->b);

            if (m_updateBuffer[bufferPosition]->mode == DMDMode::Data)
            {
              if (memcmp(renderBuffer, m_updateBuffer[bufferPosition]->data, length) != 0)
              {
                memcpy(renderBuffer, m_updateBuffer[bufferPosition]->data, length);
                update = true;
              }
            }
            else if (m_updateBuffer[bufferPosition]->mode == DMDMode::AlphaNumeric)
            {
              if (memcmp(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t)) != 0)
              {
                memcpy(segData1, m_updateBuffer[bufferPosition]->segData, 128 * sizeof(uint16_t));
                update = true;
              }

              if (m_updateBuffer[bufferPosition]->hasSegData2 &&
                  memcmp(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t)) != 0)
              {
                memcpy(segData2, m_updateBuffer[bufferPosition]->segData2, 128 * sizeof(uint16_t));
                update = true;
              }

              if (update)
              {
                if (m_updateBuffer[bufferPosition]->hasSegData2)
                  m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1, segData2);
                else
                  m_pAlphaNumeric->Render(renderBuffer, m_updateBuffer[bufferPosition]->layout, segData1);
              }
            }
          }

          if (update)
          {
            for (int i = 0; i < length; i++)
            {
              int palettePos = renderBuffer[i] * 3;
              int pos = i * 3;
              rgb24Data[pos] = palette[palettePos];
              rgb24Data[pos + 1] = palette[palettePos + 1];
              rgb24Data[pos + 2] = palette[palettePos + 2];
            }

            for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
            {
              if (pRGB24DMD->GetLength() == length * 3) pRGB24DMD->Update(rgb24Data);
            }
          }
        }
      }

      if (++bufferPosition >= DMDUTIL_FRAME_BUFFER_SIZE) bufferPosition = 0;
    }
  }
}

bool DMD::UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b)
{
  if (depth != 2 && depth != 4) return false;
  uint8_t palette[192];
  memcpy(palette, pPalette, 192);

  memset(pPalette, 0, 192);

  const uint8_t colors = (depth == 2) ? 4 : 16;
  uint8_t pos = 0;

  for (uint8_t i = 0; i < colors; i++)
  {
    float perc = FrameUtil::CalcBrightness((float)i / (float)(colors - 1));
    pPalette[pos++] = (uint8_t)(((float)r) * perc);
    pPalette[pos++] = (uint8_t)(((float)g) * perc);
    pPalette[pos++] = (uint8_t)(((float)b) * perc);
  }

  return (memcmp(pPalette, palette, 192) != 0);
}

void DMD::AdjustRGB24Depth(uint8_t* pData, uint8_t* pDstData, int length, uint8_t* palette, uint8_t depth)
{
  if (depth != 24)
  {
    for (int i = 0; i < length; i++)
    {
      int pos = i * 3;
      uint32_t r = pData[pos];
      uint32_t g = pData[pos + 1];
      uint32_t b = pData[pos + 2];

      int v = (int)(0.2126f * (float)r + 0.7152f * (float)g + 0.0722f * (float)b);
      if (v > 255) v = 255;

      uint8_t level;
      if (depth == 2)
        level = (uint8_t)(v >> 6);
      else
        level = (uint8_t)(v >> 4);

      int pos2 = level * 3;
      r = palette[pos2];
      g = palette[pos2 + 1];
      b = palette[pos2 + 2];

      pDstData[pos] = (uint8_t)r;
      pDstData[pos + 1] = (uint8_t)g;
      pDstData[pos + 2] = (uint8_t)b;
    }
  }
  else
  {
    memcpy(pDstData, pData, length);
  }
}

}  // namespace DMDUtil
