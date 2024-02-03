#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/VirtualDMD.h"

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
#include "PixelcadeDMD.h"
#endif
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

DMD::DMD(int width, int height, bool sam, const char* name)
{
  m_width = width;
  m_height = height;
  m_length = width * height;
  m_sam = sam;
  m_pBuffer = (uint8_t*)malloc(m_length);
  memset(m_pBuffer, 0, m_length);
  m_pRGB24Buffer = (uint8_t*)malloc(m_length * 3);
  memset(m_pRGB24Buffer, 0, m_length * 3);
  memset(m_segData1, 0, 128 * sizeof(uint16_t));
  memset(m_segData2, 0, 128 * sizeof(uint16_t));
  m_pLevelData = (uint8_t*)malloc(m_length);
  memset(m_pLevelData, 0, m_length);
  m_pRGB24Data = (uint8_t*)malloc(m_length * 3);
  memset(m_pRGB24Data, 0, m_length * 3);
  m_pRGB565Data = (uint16_t*)malloc(m_length * sizeof(uint16_t));
  memset(m_pRGB565Data, 0, m_length * sizeof(uint16_t));
  memset(m_palette, 0, 192);
  m_pAlphaNumeric = new AlphaNumeric();
  m_pSerum = (Config::GetInstance()->IsAltColor() && name != nullptr && name[0] != '\0') ? Serum::Load(name) : nullptr;
  m_pZeDMD = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  m_pPixelcadeDMD = nullptr;
#endif
  m_pThread = nullptr;
  m_running = false;

  FindDevices();

  Run();
}

DMD::~DMD()
{
  if (m_pThread)
  {
    m_running = false;

    m_pThread->join();
    delete m_pThread;
    m_pThread = nullptr;
  }

  while (!m_updates.empty())
  {
    DMDUpdate* const pUpdate = m_updates.front();
    m_updates.pop();
    free(pUpdate->pData);
    free(pUpdate->pData2);
    delete pUpdate;
  }

  free(m_pBuffer);
  free(m_pRGB24Buffer);
  free(m_pLevelData);
  free(m_pRGB24Data);
  free(m_pRGB565Data);
  delete m_pAlphaNumeric;
  delete m_pSerum;
  delete m_pZeDMD;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  delete m_pPixelcadeDMD;
#endif

  for (VirtualDMD* pVirtualDMD : m_virtualDMDs) delete pVirtualDMD;
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

VirtualDMD* DMD::CreateVirtualDMD()
{
  VirtualDMD* const pVirtualDMD = new VirtualDMD(m_width, m_height);
  m_virtualDMDs.push_back(pVirtualDMD);
  return pVirtualDMD;
}

bool DMD::DestroyVirtualDMD(VirtualDMD* pVirtualDMD)
{
  auto it = std::find(m_virtualDMDs.begin(), m_virtualDMDs.end(), pVirtualDMD);
  if (it != m_virtualDMDs.end())
  {
    m_virtualDMDs.erase(it);
    delete pVirtualDMD;

    return true;
  }
  return false;
}

void DMD::UpdateData(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b)
{
  DMDUpdate* const pUpdate = new DMDUpdate();
  memset(pUpdate, 0, sizeof(DMDUpdate));
  pUpdate->mode = DmdMode::Data;
  pUpdate->depth = depth;
  if (pData)
  {
    pUpdate->pData = malloc(m_length);
    memcpy(pUpdate->pData, pData, m_length);
  }
  pUpdate->r = r;
  pUpdate->g = g;
  pUpdate->b = b;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_updates.push(pUpdate);
  }
}

void DMD::UpdateRGB24Data(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b)
{
  DMDUpdate* const pUpdate = new DMDUpdate();
  memset(pUpdate, 0, sizeof(DMDUpdate));
  pUpdate->mode = DmdMode::RGB24;
  pUpdate->depth = depth;
  if (pData)
  {
    pUpdate->pData = malloc(m_length * 3);
    memcpy(pUpdate->pData, pData, m_length * 3);
  }
  pUpdate->r = r;
  pUpdate->g = g;
  pUpdate->b = b;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_updates.push(pUpdate);
  }
}

void DMD::UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                                 uint8_t g, uint8_t b)
{
  DMDUpdate* const pUpdate = new DMDUpdate();
  memset(pUpdate, 0, sizeof(DMDUpdate));
  pUpdate->mode = DmdMode::AlphaNumeric;
  pUpdate->layout = layout;
  pUpdate->depth = 2;
  pUpdate->pData = malloc(128 * sizeof(uint16_t));
  memcpy(pUpdate->pData, pData1, 128 * sizeof(uint16_t));
  if (pData2)
  {
    pUpdate->pData2 = malloc(128 * sizeof(uint16_t));
    memcpy(pUpdate->pData2, pData2, 128 * sizeof(uint16_t));
  }
  pUpdate->r = r;
  pUpdate->g = g;
  pUpdate->b = b;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_updates.push(pUpdate);
  }
}

void DMD::FindDevices()
{
  if (m_finding) return;

  m_finding = true;

  new std::thread(
      [this]()
      {
        ZeDMD* pZeDMD = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
        PixelcadeDMD* pPixelcadeDMD = nullptr;
#endif

        Config* const pConfig = Config::GetInstance();
        if (pConfig->IsZeDMD())
        {
          pZeDMD = new ZeDMD();
          pZeDMD->SetLogCallback(ZeDMDLogCallback, nullptr);

          if (pConfig->GetZeDMDDevice() != nullptr && pConfig->GetZeDMDDevice()[0] != '\0')
            pZeDMD->SetDevice(pConfig->GetZeDMDDevice());

          if (pZeDMD->Open(m_width, m_height))
          {
            if (pConfig->IsZeDMDDebug()) pZeDMD->EnableDebug();

            if (pConfig->GetZeDMDRGBOrder() != -1) pZeDMD->SetRGBOrder(pConfig->GetZeDMDRGBOrder());

            if (pConfig->GetZeDMDBrightness() != -1) pZeDMD->SetBrightness(pConfig->GetZeDMDBrightness());

            if (pConfig->IsZeDMDSaveSettings()) pZeDMD->SaveSettings();
          }
          else
          {
            delete pZeDMD;
            pZeDMD = nullptr;
          }
        }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
        if (pConfig->IsPixelcade())
          pPixelcadeDMD = PixelcadeDMD::Connect(pConfig->GetPixelcadeDevice(), m_width, m_height);
#endif

        m_pZeDMD = pZeDMD;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
        m_pPixelcadeDMD = pPixelcadeDMD;
#endif

        m_finding = false;
      });
}

void DMD::Run()
{
  if (m_running) return;

  m_running = true;

  m_pThread = new std::thread(
      [this]()
      {
        Log("DMD run thread starting");

        DmdMode mode = DmdMode::Unknown;

        while (m_running)
        {
          DMDUpdate* pUpdate = nullptr;

          {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_updates.empty())
            {
              pUpdate = m_updates.front();
              m_updates.pop();
            }
          }

          if (pUpdate)
          {
            const bool update = (mode != pUpdate->mode);
            mode = pUpdate->mode;

            if (mode == DmdMode::Data)
              UpdateData(pUpdate, update);
            else if (mode == DmdMode::RGB24)
              UpdateRGB24Data(pUpdate, update);
            else if (mode == DmdMode::AlphaNumeric)
              UpdateAlphaNumericData(pUpdate, update);

            free(pUpdate->pData);
            free(pUpdate->pData2);
            delete pUpdate;
          }
          else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        Log("DMD run thread finished");
      });
}

bool DMD::UpdatePalette(const DMDUpdate* pUpdate)
{
  if (pUpdate->depth != 2 && pUpdate->depth != 4) return false;

  uint8_t palette[192];
  memcpy(palette, m_palette, 192);

  memset(m_palette, 0, 192);

  const float r = (float)pUpdate->r;
  const float g = (float)pUpdate->g;
  const float b = (float)pUpdate->b;

  const int colors = (pUpdate->depth == 2) ? 4 : 16;
  int pos = 0;

  for (int i = 0; i < colors; i++)
  {
    float perc = FrameUtil::CalcBrightness((float)i / (float)(colors - 1));
    m_palette[pos++] = (uint8_t)(r * perc);
    m_palette[pos++] = (uint8_t)(g * perc);
    m_palette[pos++] = (uint8_t)(b * perc);
  }

  return (memcmp(m_palette, palette, 192) != 0);
}

void DMD::UpdateData(const DMDUpdate* pUpdate, bool update)
{
  uint8_t* const pData = (uint8_t*)pUpdate->pData;

  if (pData)
  {
    if (pUpdate->depth == 2)
    {
      for (int i = 0; i < m_length; i++) m_pLevelData[i] = LEVELS_WPC[pData[i]];
    }
    else if (pUpdate->depth == 4)
    {
      if (!m_sam)
      {
        for (int i = 0; i < m_length; i++) m_pLevelData[i] = LEVELS_GTS3[pData[i]];
      }
      else
      {
        for (int i = 0; i < m_length; i++) m_pLevelData[i] = LEVELS_SAM[pData[i]];
      }
    }
  }

  if (!m_pSerum)
  {
    if (pData)
    {
      if (memcmp(m_pBuffer, pData, m_length) != 0)
      {
        memcpy(m_pBuffer, pData, m_length);
        update = true;
      }
    }

    if (UpdatePalette(pUpdate)) update = true;
  }
  else if (m_pSerum->Convert(pData, m_pBuffer, m_palette))
  {
    // if we have serum, run a conversion, and if success, we have an update (needed for rotations)
    // serum will take care of updating the data buffer
    update = true;
  }

  if (!update) return;

  for (int i = 0; i < m_length; i++)
  {
    int pos = m_pBuffer[i] * 3;
    uint32_t r = m_palette[pos];
    uint32_t g = m_palette[pos + 1];
    uint32_t b = m_palette[pos + 2];

    pos = i * 3;
    m_pRGB24Data[pos] = r;
    m_pRGB24Data[pos + 1] = g;
    m_pRGB24Data[pos + 2] = b;

    m_pRGB565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
  }

  if (m_pZeDMD)
  {
    if (m_pSerum)
    {
      m_pZeDMD->SetPalette(m_palette, 64);
      m_pZeDMD->RenderColoredGray6(m_pBuffer, nullptr);
    }
    else
    {
      if (pUpdate->depth == 2)
      {
        m_pZeDMD->SetPalette(m_palette, 4);
        m_pZeDMD->RenderGray2(m_pBuffer);
      }
      else
      {
        m_pZeDMD->SetPalette(m_palette, 16);
        m_pZeDMD->RenderGray4(m_pBuffer);
      }
    }
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMD) m_pPixelcadeDMD->Update(m_pRGB565Data);
#endif

  for (VirtualDMD* pVirtualDMD : m_virtualDMDs) pVirtualDMD->Update(m_pLevelData, m_pRGB24Data);
}

void DMD::UpdateRGB24Data(const DMDUpdate* pUpdate, bool update)
{
  uint8_t* const pData = (uint8_t*)pUpdate->pData;

  if (pUpdate->depth != 24)
  {
    if (UpdatePalette(pUpdate)) update = true;
  }

  if (memcmp(m_pRGB24Buffer, pData, m_length * 3) != 0) update = true;

  if (!update) return;

  memcpy(m_pRGB24Buffer, pData, m_length * 3);

  for (int i = 0; i < m_length; i++)
  {
    int pos = i * 3;
    uint32_t r = m_pRGB24Buffer[pos];
    uint32_t g = m_pRGB24Buffer[pos + 1];
    uint32_t b = m_pRGB24Buffer[pos + 2];

    if (pUpdate->depth != 24)
    {
      int v = (int)(0.2126f * (float)r + 0.7152f * (float)g + 0.0722f * (float)b);
      if (v > 255) v = 255;

      uint8_t level;
      if (pUpdate->depth == 2)
        level = (uint8_t)(v >> 6);
      else
        level = (uint8_t)(v >> 4);

      m_pLevelData[i] = level;

      int pos2 = level * 3;
      r = m_palette[pos2];
      g = m_palette[pos2 + 1];
      b = m_palette[pos2 + 2];
    }

    m_pRGB24Data[pos] = (uint8_t)r;
    m_pRGB24Data[pos + 1] = (uint8_t)g;
    m_pRGB24Data[pos + 2] = (uint8_t)b;

    m_pRGB565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
  }

  if (pUpdate->depth == 2)
  {
    if (m_pZeDMD)
    {
      m_pZeDMD->SetPalette(m_palette, 4);
      m_pZeDMD->RenderGray2(m_pLevelData);
    }
  }
  else if (pUpdate->depth == 4)
  {
    if (m_pZeDMD)
    {
      m_pZeDMD->SetPalette(m_palette, 16);
      m_pZeDMD->RenderGray4(m_pLevelData);
    }
  }
  else if (pUpdate->depth == 24)
  {
    if (m_pZeDMD) m_pZeDMD->RenderRgb24((uint8_t*)m_pRGB24Buffer);
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMD) m_pPixelcadeDMD->Update(m_pRGB565Data);
#endif

  for (VirtualDMD* pVirtualDMD : m_virtualDMDs) pVirtualDMD->Update(m_pLevelData, m_pRGB24Data);
}

void DMD::UpdateAlphaNumericData(const DMDUpdate* pUpdate, bool update)
{
  if (memcmp(m_segData1, pUpdate->pData, 128 * sizeof(uint16_t)) != 0)
  {
    memcpy(m_segData1, pUpdate->pData, 128 * sizeof(uint16_t));
    update = true;
  }

  if (pUpdate->pData2 && memcmp(m_segData2, pUpdate->pData2, 128 * sizeof(uint16_t)) != 0)
  {
    memcpy(m_segData2, pUpdate->pData2, 128 * sizeof(uint16_t));
    update = true;
  }

  if (UpdatePalette(pUpdate)) update = true;

  if (!update) return;

  uint8_t* pData;

  if (pUpdate->pData2)
    pData = m_pAlphaNumeric->Render(pUpdate->layout, (const uint16_t*)m_segData1, (const uint16_t*)m_segData2);
  else
    pData = m_pAlphaNumeric->Render(pUpdate->layout, (const uint16_t*)m_segData1);

  for (int i = 0; i < m_length; i++) m_pLevelData[i] = LEVELS_WPC[pData[i]];

  for (int i = 0; i < m_length; i++)
  {
    int pos = pData[i] * 3;
    uint32_t r = m_palette[pos];
    uint32_t g = m_palette[pos + 1];
    uint32_t b = m_palette[pos + 2];

    pos = i * 3;
    m_pRGB24Data[pos] = (uint8_t)r;
    m_pRGB24Data[pos + 1] = (uint8_t)g;
    m_pRGB24Data[pos + 2] = (uint8_t)b;

    m_pRGB565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
  }

  if (m_pZeDMD)
  {
    m_pZeDMD->SetPalette(m_palette, 4);
    m_pZeDMD->RenderGray2(pData);
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMD) m_pPixelcadeDMD->Update(m_pRGB565Data);
#endif

  for (VirtualDMD* pVirtualDMD : m_virtualDMDs) pVirtualDMD->Update(m_pLevelData, m_pRGB24Data);
}

}  // namespace DMDUtil