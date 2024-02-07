#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/VirtualDMD.h"

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
  for (uint8_t i = 0; i < DMD_FRAME_BUFFER_SIZE; i++) {
    m_updateBuffer[i] = new DMDUpdate();
    memset(&m_updateBuffer[i], 0, sizeof(DMDUpdate));
  }
  m_pAlphaNumeric = new AlphaNumeric();
  m_pSerum = (Config::GetInstance()->IsAltColor() && name != nullptr && name[0] != '\0') ? Serum::Load(name) : nullptr;
  m_pZeDMD = nullptr;
  m_pZeDMDThread = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  m_pPixelcadeDMD = nullptr;
  m_pPixelcadeDMDThread = nullptr;
#endif

  FindDevices();

  if (m_pZeDMD) {
    m_pZeDMDThread = new std::thread(&DMD::ZeDMDThread, this);
  }

#if !((defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || \
                              (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
      defined(__ANDROID__))
  if (m_pPixelcadeDMD) {
    m_pPixelcadeDMDThread = new std::thread(&DMD::PixelcadeThread, this);
  }
#endif

  m_pdmdFrameReadyResetThread = new std::thread(&DMD::DmdFrameReadyResetThread, this);
}

DMD::~DMD() {
  m_pdmdFrameReadyResetThread->join();
  delete m_pdmdFrameReadyResetThread;
  m_pdmdFrameReadyResetThread = nullptr;

  if (m_pZeDMDThread) {
    m_pZeDMDThread->join();
    delete m_pZeDMDThread;
    m_pZeDMDThread = nullptr;
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMDThread) {
    m_pPixelcadeDMDThread->join();
    delete m_pPixelcadeDMDThread;
    m_pPixelcadeDMDThread = nullptr;
  }
#endif
  /*
    while (!m_updates.empty()) {
      DMDUpdate* const pUpdate = m_updates.front();
      m_updates.pop();
      free(pUpdate->pData);
      free(pUpdate->pData2);
      delete pUpdate;
    }
  */
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

void DMD::UpdateData(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b, DmdMode mode)
{
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  m_updateBuffer[m_updateBufferPosition]->mode = mode;
  m_updateBuffer[m_updateBufferPosition]->depth = depth;
  // @todo width, height and length have to be dynamic.
  m_updateBuffer[m_updateBufferPosition]->width = m_width;
  m_updateBuffer[m_updateBufferPosition]->height = m_height;
  if (m_updateBuffer[m_updateBufferPosition]->pData != nullptr) {
    free(m_updateBuffer[m_updateBufferPosition]->pData);
    m_updateBuffer[m_updateBufferPosition]->pData = nullptr;
  }
  if (m_updateBuffer[m_updateBufferPosition]->pData2 != nullptr) {
    free(m_updateBuffer[m_updateBufferPosition]->pData2);
    m_updateBuffer[m_updateBufferPosition]->pData2 = nullptr;
  }
  if (pData) {
    m_updateBuffer[m_updateBufferPosition]->pData =
        malloc(m_width * m_height * (mode == DmdMode::RGB24 ? 3 : 1));
    memcpy(m_updateBuffer[m_updateBufferPosition]->pData, pData,
           m_width * m_height * (mode == DmdMode::RGB24 ? 3 : 1));
  }
  m_updateBuffer[m_updateBufferPosition]->r = r;
  m_updateBuffer[m_updateBufferPosition]->g = g;
  m_updateBuffer[m_updateBufferPosition]->b = b;

  m_dmdFrameReady = true;
  ul.unlock();
  m_dmdCV.notify_all();

  if (++m_updateBufferPosition > DMD_FRAME_BUFFER_SIZE) {
    m_updateBufferPosition = 0;
  }
}

void DMD::UpdateRGB24Data(const uint8_t* pData, int depth, uint8_t r, uint8_t g, uint8_t b)
{
  UpdateData(pData, depth, r, g, b, DmdMode::RGB24);
}

void DMD::UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                                 uint8_t g, uint8_t b)
{
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  m_updateBuffer[m_updateBufferPosition]->mode = DmdMode::AlphaNumeric;
  m_updateBuffer[m_updateBufferPosition]->depth = 2;
  m_updateBuffer[m_updateBufferPosition]->width = 128;
  m_updateBuffer[m_updateBufferPosition]->height = 32;
  if (m_updateBuffer[m_updateBufferPosition]->pData != nullptr) {
    free(m_updateBuffer[m_updateBufferPosition]->pData);
    m_updateBuffer[m_updateBufferPosition]->pData = nullptr;
  }
  if (m_updateBuffer[m_updateBufferPosition]->pData2 != nullptr) {
    free(m_updateBuffer[m_updateBufferPosition]->pData2);
    m_updateBuffer[m_updateBufferPosition]->pData2 = nullptr;
  }
  m_updateBuffer[m_updateBufferPosition]->pData = malloc(128 * sizeof(uint16_t));
  memcpy(m_updateBuffer[m_updateBufferPosition]->pData, pData1,
         128 * sizeof(uint16_t));
  if (pData2) {
    m_updateBuffer[m_updateBufferPosition]->pData2 =
        malloc(128 * sizeof(uint16_t));
    memcpy(m_updateBuffer[m_updateBufferPosition]->pData2, pData2,
           128 * sizeof(uint16_t));
  }
  m_updateBuffer[m_updateBufferPosition]->r = r;
  m_updateBuffer[m_updateBufferPosition]->g = g;
  m_updateBuffer[m_updateBufferPosition]->b = b;

  m_dmdFrameReady = true;
  ul.unlock();
  m_dmdCV.notify_all();

  if (++m_updateBufferPosition > DMD_FRAME_BUFFER_SIZE) {
    m_updateBufferPosition = 0;
  }
}

void DMD::DmdFrameReadyResetThread() {
  while (true) {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m_dmdFrameReady = false;

    if (m_stopFlag) {
      return;
    }
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

void DMD::ZeDMDThread() {
  int bufferPosition = 0;
  uint16_t prevWidth = 0;
  uint16_t prevHeight = 0;
  uint16_t segData1[128];
  uint16_t segData2[128];
  uint8_t palette[192] = {0};

  while (true) {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag) {
      return;
    }

    while (bufferPosition != m_updateBufferPosition) {
      if (m_updateBuffer[bufferPosition]->width != prevWidth ||
          m_updateBuffer[bufferPosition]->height != prevHeight) {
        prevWidth = m_updateBuffer[bufferPosition]->width;
        prevHeight = m_updateBuffer[bufferPosition]->height;
        m_pZeDMD->SetFrameSize(prevWidth, prevHeight);
      }
      if (m_updateBuffer[bufferPosition]->mode == DmdMode::RGB24) {
        m_pZeDMD->RenderRgb24((uint8_t*)m_updateBuffer[bufferPosition]->pData);
        continue;
      }

      bool update = UpdatePalette(palette, m_updateBuffer[bufferPosition]->depth,
                                  m_updateBuffer[bufferPosition]->r,
                                  m_updateBuffer[bufferPosition]->g,
                                  m_updateBuffer[bufferPosition]->b);

      if (m_updateBuffer[bufferPosition]->mode == DmdMode::Data) {
        uint8_t renderBuffer[prevWidth * prevHeight];
        memcpy(renderBuffer, m_updateBuffer[bufferPosition]->pData,
               prevWidth * prevHeight);

        if (m_pSerum) {
          uint8_t rotations[24] = {0};
          uint32_t triggerID;
          uint32_t hashcode;
          int frameID;

          m_pSerum->SetStandardPalette(palette, m_updateBuffer[bufferPosition]->depth);

          if (m_pSerum->ColorizeWithMetadata(
                  renderBuffer, prevWidth, prevHeight, &palette[0],
                  &rotations[0], &triggerID, &hashcode, &frameID)) {
            m_pZeDMD->RenderColoredGray6(renderBuffer, palette, rotations);

            // @todo: send DMD PUP Event with triggerID
          }
        } else {
          m_pZeDMD->SetPalette(palette);

          switch (m_updateBuffer[bufferPosition]->depth) {
            case 2:
              m_pZeDMD->RenderGray2(renderBuffer);
              break;

            case 4:
              m_pZeDMD->RenderGray4(renderBuffer);
              break;

            //default:
              //@todo log error
          }
        }

      } else if (m_updateBuffer[bufferPosition]->mode == DmdMode::AlphaNumeric) {
        if (memcmp(segData1, m_updateBuffer[bufferPosition]->pData,
                   128 * sizeof(uint16_t)) != 0) {
          memcpy(segData1, m_updateBuffer[bufferPosition]->pData,
                 128 * sizeof(uint16_t));
          update = true;
        }

        if (m_updateBuffer[bufferPosition]->pData2 &&
            memcmp(segData2, m_updateBuffer[bufferPosition]->pData2,
                   128 * sizeof(uint16_t)) != 0) {
          memcpy(segData2, m_updateBuffer[bufferPosition]->pData2,
                 128 * sizeof(uint16_t));
          update = true;
        }

        if (!update) continue;

        uint8_t* pData;

        if (m_updateBuffer[bufferPosition]->pData2)
          pData = m_pAlphaNumeric->Render(m_updateBuffer[bufferPosition]->layout,
                                          (const uint16_t*)segData1,
                                          (const uint16_t*)segData2);
        else
          pData = m_pAlphaNumeric->Render(m_updateBuffer[bufferPosition]->layout,
                                          (const uint16_t*)segData1);

        m_pZeDMD->SetPalette(m_palette, 4);
        m_pZeDMD->RenderGray2(pData);
      }

      if (++bufferPosition >= DMD_FRAME_BUFFER_SIZE) {
        bufferPosition = 0;
      }
    }
  }
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

#if !((defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || \
                              (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
      defined(__ANDROID__))

void DMD::PixelcadeDMDThread() {
  int bufferPosition = 0;

  while (true) {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(sl, [&]() { return m_dmdFrameReady || m_stopFlag; });
    sl.unlock();
    if (m_stopFlag) {
      return;
    }

    while (bufferPosition != m_updateBufferPosition) {
    }
  }
} 
#endif

}  // namespace DMDUtil
