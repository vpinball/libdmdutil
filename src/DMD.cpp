#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/ConsoleDMD.h"
#include "DMDUtil/LevelDMD.h"
#include "DMDUtil/RGB24DMD.h"
#include "DMDUtil/SceneGenerator.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>  // Windows byte-order functions
#else
#include <arpa/inet.h>  // Linux/macOS byte-order functions
#endif

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
#include "PixelcadeDMD.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_set>

#include "AlphaNumeric.h"
#include "FrameUtil.h"
#include "Logger.h"
#include "ZeDMD.h"
#include "komihash/komihash.h"
#include "pupdmd.h"
#include "serum-decode.h"
#include "serum.h"
#include "sockpp/tcp_connector.h"

namespace DMDUtil
{

void PUPDMDCALLBACK PUPDMDLogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log(DMDUtil_LogLevel_INFO, "%s", buffer);
}

void ZEDMDCALLBACK ZeDMDLogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log(DMDUtil_LogLevel_INFO, "%s", buffer);
}

class DMDServerConnector
{
 public:
  ~DMDServerConnector() { delete m_pConnector; }

  static DMDServerConnector* Create(const char* pAddress, int port)
  {
    sockpp::tcp_connector* pConnector = new sockpp::tcp_connector({pAddress, (in_port_t)port});
    if (pConnector)
    {
      return new DMDServerConnector(pConnector);
    }
    return nullptr;
  }

  ssize_t Write(const void* buf, size_t size) { return m_pConnector->write_n(buf, size); }

  void Close() { m_pConnector->close(); }

 private:
  DMDServerConnector(sockpp::tcp_connector* pConnector) : m_pConnector(pConnector) {}
  sockpp::tcp_connector* m_pConnector;
};

bool DMD::m_finding = false;

void DMD::Update::convertToHostByteOrder()
{
  // uint8_t and bool are not converted, as they are already in host byte order.
  mode = static_cast<Mode>(ntohl(static_cast<uint32_t>(mode)));
  layout = static_cast<AlphaNumericLayout>(ntohl(static_cast<uint32_t>(layout)));
  depth = ntohl(depth);
  for (size_t i = 0; i < 256 * 64; i++)
  {
    segData[i] = ntohs(segData[i]);
  }
  for (size_t i = 0; i < 128; i++)
  {
    segData2[i] = ntohs(segData2[i]);
  }
  width = ntohs(width);
  height = ntohs(height);
}

DMD::Update DMD::Update::toNetworkByteOrder() const
{
  // uint8_t and bool are not converted, as they are already in network byte order.
  Update copy = *this;
  copy.mode = static_cast<Mode>(htonl(static_cast<int>(mode)));
  copy.layout = static_cast<AlphaNumericLayout>(htonl(static_cast<int>(layout)));
  copy.depth = htonl(depth);
  for (size_t i = 0; i < 256 * 64; i++)
  {
    copy.segData[i] = htons(segData[i]);
  }
  for (size_t i = 0; i < 128; i++)
  {
    copy.segData2[i] = htons(segData2[i]);
  }
  copy.width = htons(width);
  copy.height = htons(height);
  return copy;
}

void DMD::StreamHeader::convertToHostByteOrder()
{
  // uint8_t and char are not converted, as they are already in host byte order.
  mode = static_cast<Mode>(ntohl(static_cast<int>(mode)));
  width = ntohs(width);
  height = ntohs(height);
  length = ntohl(length);
}

void DMD::StreamHeader::convertToNetworkByteOrder()
{
  // uint8_t and char are not converted, as they are already in network byte order.
  mode = static_cast<Mode>(htonl(static_cast<int>(mode)));
  width = htons(width);
  height = htons(height);
  length = htonl(length);
}

DMD::DMD()
{
  for (uint8_t i = 0; i < DMDUTIL_FRAME_BUFFER_SIZE; i++)
  {
    m_pUpdateBufferQueue[i] = new Update();
  }
  m_updateBufferQueuePosition.store(0, std::memory_order_release);
  m_stopFlag.store(false, std::memory_order_release);
  m_dmdFrameReady.store(false, std::memory_order_release);
  m_updateBuffered = std::make_shared<Update>();

  m_pAlphaNumeric = new AlphaNumeric();
  m_pGenerator = new SceneGenerator();
  m_pSerum = nullptr;
  m_pZeDMD = nullptr;
  m_pPUPDMD = nullptr;

  m_pZeDMDThread = nullptr;
  m_pLevelDMDThread = nullptr;
  m_pRGB24DMDThread = nullptr;
  m_pConsoleDMDThread = nullptr;
  m_pDumpDMDTxtThread = nullptr;
  m_pDumpDMDRawThread = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  m_pPixelcadeDMD = nullptr;
  m_pPixelcadeDMDThread = nullptr;
#endif

  m_pDmdFrameThread = new std::thread(&DMD::DmdFrameThread, this);
  m_pPupDMDThread = new std::thread(&DMD::PupDMDThread, this);
  m_pSerumThread = new std::thread(&DMD::SerumThread, this);
  m_pDMDServerConnector = nullptr;
}

DMD::~DMD()
{
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  m_stopFlag.store(true, std::memory_order_release);
  ul.unlock();
  m_dmdCV.notify_all();

  m_pDmdFrameThread->join();
  delete m_pDmdFrameThread;
  m_pDmdFrameThread = nullptr;

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

  if (m_pConsoleDMDThread)
  {
    m_pConsoleDMDThread->join();
    delete m_pConsoleDMDThread;
    m_pConsoleDMDThread = nullptr;
  }

  if (m_pZeDMDThread)
  {
    m_pZeDMDThread->join();
    delete m_pZeDMDThread;
    m_pZeDMDThread = nullptr;
  }

  if (m_pDumpDMDTxtThread)
  {
    m_pDumpDMDTxtThread->join();
    delete m_pDumpDMDTxtThread;
    m_pDumpDMDTxtThread = nullptr;
  }

  if (m_pDumpDMDRawThread)
  {
    m_pDumpDMDRawThread->join();
    delete m_pDumpDMDRawThread;
    m_pDumpDMDRawThread = nullptr;
  }

  if (m_pPupDMDThread)
  {
    m_pPupDMDThread->join();
    delete m_pPupDMDThread;
    m_pPupDMDThread = nullptr;
  }

  if (m_pSerumThread)
  {
    m_pSerumThread->join();
    delete m_pSerumThread;
    m_pSerumThread = nullptr;
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
  delete m_pGenerator;
  delete m_pZeDMD;
  delete m_pPUPDMD;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMD) delete m_pPixelcadeDMD;
#endif

  for (LevelDMD* pLevelDMD : m_levelDMDs) delete pLevelDMD;
  for (RGB24DMD* pRGB24DMD : m_rgb24DMDs) delete pRGB24DMD;
  for (ConsoleDMD* pConsoleDMD : m_consoleDMDs) delete pConsoleDMD;

  if (m_pDMDServerConnector)
  {
    m_pDMDServerConnector->Close();
    delete m_pDMDServerConnector;
    m_pDMDServerConnector = nullptr;
  }
}

bool DMD::ConnectDMDServer()
{
  if (!m_pDMDServerConnector)
  {
    Config* const pConfig = Config::GetInstance();
    sockpp::initialize();
    Log(DMDUtil_LogLevel_INFO, "Connecting DMDServer on %s:%d", pConfig->GetDMDServerAddr(),
        pConfig->GetDMDServerPort());
    m_pDMDServerConnector = DMDServerConnector::Create(pConfig->GetDMDServerAddr(), pConfig->GetDMDServerPort());
    if (!m_pDMDServerConnector)
    {
      Log(DMDUtil_LogLevel_INFO, "DMDServer connection to %s:%d failed!", pConfig->GetDMDServerAddr(),
          pConfig->GetDMDServerPort());
    }
  }
  return (m_pDMDServerConnector);
}

bool DMD::IsFinding() { return m_finding; }

bool DMD::HasDisplay() const
{
  if (m_pZeDMD != nullptr && m_rgb24DMDs.size() > 0)
  {
    return true;
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  return (m_pPixelcadeDMD != nullptr);
#endif

  return false;
}

bool DMD::HasHDDisplay() const
{
  if (m_pZeDMD != nullptr && m_pZeDMD->GetWidth() == 256) return true;

  if (m_rgb24DMDs.size() > 0)
  {
    for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
    {
      if (pRGB24DMD->GetWidth() == 256) return true;
    }
  }

  return false;
}

void DMD::SetRomName(const char* name) { strcpy(m_romName, name ? name : ""); }

void DMD::SetAltColorPath(const char* path) { strcpy(m_altColorPath, path ? path : ""); }

void DMD::SetPUPVideosPath(const char* path) { strcpy(m_pupVideosPath, path ? path : ""); }

void DMD::DumpDMDTxt()
{
  if (!m_pDumpDMDTxtThread)
  {
    m_pDumpDMDTxtThread = new std::thread(&DMD::DumpDMDTxtThread, this);
  }
}

void DMD::DumpDMDRaw()
{
  if (m_pDumpDMDRawThread)
  {
    m_pDumpDMDRawThread = new std::thread(&DMD::DumpDMDRawThread, this);
  }
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

void DMD::AddRGB24DMD(RGB24DMD* pRGB24DMD)
{
  m_rgb24DMDs.push_back(pRGB24DMD);
  Log(DMDUtil_LogLevel_INFO, "Added RGB24DMD");
  if (!m_pRGB24DMDThread)
  {
    m_pRGB24DMDThread = new std::thread(&DMD::RGB24DMDThread, this);
    Log(DMDUtil_LogLevel_INFO, "RGB24DMDThread started");
  }
}

RGB24DMD* DMD::CreateRGB24DMD(uint16_t width, uint16_t height)
{
  RGB24DMD* const pRGB24DMD = new RGB24DMD(width, height);
  AddRGB24DMD(pRGB24DMD);
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

ConsoleDMD* DMD::CreateConsoleDMD(bool overwrite, FILE* out)
{
  ConsoleDMD* const pConsoleDMD = new ConsoleDMD(overwrite, out);
  m_consoleDMDs.push_back(pConsoleDMD);
  if (!m_pConsoleDMDThread) m_pConsoleDMDThread = new std::thread(&DMD::ConsoleDMDThread, this);
  return pConsoleDMD;
}

bool DMD::DestroyConsoleDMD(ConsoleDMD* pConsoleDMD)
{
  auto it = std::find(m_consoleDMDs.begin(), m_consoleDMDs.end(), pConsoleDMD);
  if (it != m_consoleDMDs.end())
  {
    m_consoleDMDs.erase(it);
    delete pConsoleDMD;

    if (m_consoleDMDs.empty())
    {
      //@todo terminate ConsoleDMDThread
    }

    return true;
  }
  return false;
}

void DMD::UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                     Mode mode, bool buffered)
{
  auto dmdUpdate = std::make_shared<Update>();
  if (pData)
  {
    memcpy(dmdUpdate->data, pData, (size_t)width * height * (mode == Mode::RGB16 ? 2 : (mode == Mode::RGB24 ? 3 : 1)));
    dmdUpdate->hasData = true;
  }
  else
  {
    dmdUpdate->hasData = false;
  }
  dmdUpdate->mode = mode;
  dmdUpdate->depth = depth;
  dmdUpdate->width = width;
  dmdUpdate->height = height;
  dmdUpdate->hasSegData = false;
  dmdUpdate->hasSegData2 = false;
  dmdUpdate->r = r;
  dmdUpdate->g = g;
  dmdUpdate->b = b;

  QueueUpdate(dmdUpdate, buffered);
}

void DMD::QueueUpdate(const std::shared_ptr<Update> dmdUpdate, bool buffered)
{
  std::thread(
      [this, dmdUpdate, buffered]()
      {
        std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
        uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
        memcpy(m_pUpdateBufferQueue[(++updateBufferQueuePosition) % DMDUTIL_FRAME_BUFFER_SIZE], dmdUpdate.get(),
               sizeof(Update));
        m_updateBufferQueuePosition.store(updateBufferQueuePosition, std::memory_order_release);
        m_dmdFrameReady.store(true, std::memory_order_release);

        Log(DMDUtil_LogLevel_DEBUG, "Queued Frame: position=%d, mode=%d, depth=%d", updateBufferQueuePosition,
            dmdUpdate->mode, dmdUpdate->depth);

        if (buffered)
        {
          memcpy(m_updateBuffered.get(), dmdUpdate.get(), sizeof(Update));
          m_hasUpdateBuffered = true;
        }

        ul.unlock();
        m_dmdCV.notify_all();

        if (m_pDMDServerConnector && !IsSerumMode(dmdUpdate->mode))
        {
          StreamHeader streamHeader;
          streamHeader.buffered = (uint8_t)buffered;
          streamHeader.disconnectOthers = (uint8_t)m_dmdServerDisconnectOthers;
          streamHeader.convertToNetworkByteOrder();
          m_pDMDServerConnector->Write(&streamHeader, sizeof(StreamHeader));
          PathsHeader pathsHeader;
          strcpy(pathsHeader.name, m_romName);
          strcpy(pathsHeader.altColorPath, m_altColorPath);
          strcpy(pathsHeader.pupVideosPath, m_pupVideosPath);
          pathsHeader.convertToNetworkByteOrder();
          m_pDMDServerConnector->Write(&pathsHeader, sizeof(PathsHeader));
          Update dmdUpdateNetwork = dmdUpdate->toNetworkByteOrder();
          m_pDMDServerConnector->Write(&dmdUpdateNetwork, sizeof(Update));

          if (streamHeader.disconnectOthers != 0) m_dmdServerDisconnectOthers = false;
        }
      })
      .detach();
}

bool DMD::QueueBuffer()
{
  if (m_hasUpdateBuffered)
  {
    QueueUpdate(m_updateBuffered, false);
  }

  return m_hasUpdateBuffered;
}

void DMD::UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                     bool buffered)
{
  UpdateData(pData, depth, width, height, r, g, b, Mode::Data, buffered);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                          uint8_t b, bool buffered)
{
  UpdateData(pData, depth, width, height, r, g, b, Mode::RGB24, buffered);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  UpdateData(pData, 24, width, height, 0, 0, 0, Mode::RGB24, buffered);
}

void DMD::UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  auto dmdUpdate = std::make_shared<Update>();
  dmdUpdate->mode = Mode::RGB16;
  dmdUpdate->depth = 24;
  dmdUpdate->width = width;
  dmdUpdate->height = height;
  if (pData)
  {
    memcpy(dmdUpdate->segData, pData, (size_t)width * height * sizeof(uint16_t));
    dmdUpdate->hasData = true;
  }
  else
  {
    dmdUpdate->hasData = false;
  }
  dmdUpdate->hasSegData = false;
  dmdUpdate->hasSegData2 = false;

  QueueUpdate(dmdUpdate, buffered);
}

void DMD::UpdateAlphaNumericData(AlphaNumericLayout layout, const uint16_t* pData1, const uint16_t* pData2, uint8_t r,
                                 uint8_t g, uint8_t b)
{
  auto dmdUpdate = std::make_shared<Update>();
  dmdUpdate->mode = Mode::AlphaNumeric;
  dmdUpdate->layout = layout;
  dmdUpdate->depth = 2;
  dmdUpdate->width = 128;
  dmdUpdate->height = 32;
  dmdUpdate->hasData = false;
  if (pData1)
  {
    memcpy(dmdUpdate->segData, pData1, 128 * sizeof(uint16_t));
    dmdUpdate->hasSegData = true;
  }
  else
  {
    dmdUpdate->hasSegData = false;
  }
  if (pData2)
  {
    memcpy(dmdUpdate->segData2, pData2, 128 * sizeof(uint16_t));
    dmdUpdate->hasSegData2 = true;
  }
  else
  {
    dmdUpdate->hasSegData2 = false;
  }
  dmdUpdate->r = r;
  dmdUpdate->g = g;
  dmdUpdate->b = b;

  QueueUpdate(dmdUpdate, false);
}

void DMD::FindDisplays()
{
  if (m_finding) return;

  Config* const pConfig = Config::GetInstance();

  if (pConfig->IsDmdServer())
  {
    ConnectDMDServer();
  }

  if (pConfig->IsLocalDisplaysActive())
  {
    m_finding = true;

    std::thread(
        [this]()
        {
          Config* const pConfig = Config::GetInstance();
          ZeDMD* pZeDMD = nullptr;

          bool openSerial = false;
          bool openWiFi = false;

          if (pConfig->IsZeDMD() || pConfig->IsZeDMDWiFiEnabled())
          {
            pZeDMD = new ZeDMD();
            pZeDMD->SetLogCallback(ZeDMDLogCallback, nullptr);
          }

          if (pConfig->IsZeDMDWiFiEnabled())
          {
            std::string WiFiAddr = pConfig->GetZeDMDWiFiAddr() ? pConfig->GetZeDMDWiFiAddr() : "zedmd-wifi.local";

            if (WiFiAddr.empty())
            {
              DMDUtil::Log(DMDUtil_LogLevel_ERROR, "ERROR: ZeDMD WiFi IP address is not configured.");
            }

            // Proceed only if the WiFiAddr is valid.
            if (!WiFiAddr.empty() && (openWiFi = pZeDMD->OpenWiFi(WiFiAddr.c_str())))
            {
              std::stringstream logMessage;
              logMessage << "ZeDMD WiFi enabled, connected to " << WiFiAddr << ".";
              DMDUtil::Log(DMDUtil_LogLevel_INFO, logMessage.str().c_str());
            }
          }

          if (pConfig->IsZeDMD())
          {
            if (pConfig->GetZeDMDDevice() != nullptr && pConfig->GetZeDMDDevice()[0] != '\0')
              pZeDMD->SetDevice(pConfig->GetZeDMDDevice());

            if ((openSerial = pZeDMD->Open()))
            {
              if (pConfig->GetZeDMDBrightness() != -1) pZeDMD->SetBrightness(pConfig->GetZeDMDBrightness());
            }
          }

          if (openSerial || openWiFi)
          {
            if (pConfig->IsZeDMDDebug()) pZeDMD->EnableDebug();
            pZeDMD->EnableUpscaling();
            m_pZeDMDThread = new std::thread(&DMD::ZeDMDThread, this);
          }
          else
          {
            delete pZeDMD;
            pZeDMD = nullptr;
          }

          m_pZeDMD = pZeDMD;

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
          PixelcadeDMD* pPixelcadeDMD = nullptr;

          if (pConfig->IsPixelcade())
          {
            pPixelcadeDMD = PixelcadeDMD::Connect(pConfig->GetPixelcadeDevice(), 128, 32);
            if (pPixelcadeDMD) m_pPixelcadeDMDThread = new std::thread(&DMD::PixelcadeDMDThread, this);
          }

          m_pPixelcadeDMD = pPixelcadeDMD;
#endif

          m_finding = false;
        })
        .detach();
  }
}

uint8_t DMD::GetNextBufferQueuePosition(uint16_t bufferPosition, const uint16_t updateBufferQueuePosition)
{
  if (bufferPosition == updateBufferQueuePosition)
  {
    return bufferPosition;  // No change, return current position
  }

  ++bufferPosition;  // 65535 + 1 = 0

  if (bufferPosition < updateBufferQueuePosition)
  {
    if ((updateBufferQueuePosition - bufferPosition) > DMDUTIL_MAX_FRAMES_BEHIND)
      return updateBufferQueuePosition - DMDUTIL_MIN_FRAMES_BEHIND;  // Too many frames behind, skip a lot
    else if ((updateBufferQueuePosition - bufferPosition) > (DMDUTIL_MAX_FRAMES_BEHIND / 2))
      return ++bufferPosition;  // Skip one frame to avoid too many frames behind
  }
  else if (bufferPosition > updateBufferQueuePosition)  // updateBufferQueuePosition crossed the overflow point
  {
    return 0;  // Reset to 0 if we crossed the overflow point, this is good enough
  }

  return bufferPosition;
}

void DMD::DmdFrameThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();

    if (strcmp(m_romName, name) != 0)
    {
      strcpy(name, m_romName);

      // In case of a new ROM, try to disconnect the other clients.
      if (m_pDMDServerConnector) m_dmdServerDisconnectOthers = true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
    m_dmdFrameReady.store(false, std::memory_order_release);
    ul.unlock();

    if (m_stopFlag)
    {
      return;
    }
  }
}

void DMD::ZeDMDThread()
{
  uint16_t bufferPosition = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t frameSize = 0;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[PALETTE_SIZE] = {0};
  uint8_t indexBuffer[256 * 64] = {0};
  uint8_t renderBuffer[256 * 64 * 3] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();

    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pSerum &&
          (!IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames) ||
           (m_pZeDMD->GetWidth() == 256 && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32_64) ||
           (m_pZeDMD->GetWidth() < 256 && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_64_32)))
        continue;

      // Note: libzedmd has its own update detection.

      if (m_pUpdateBufferQueue[bufferPositionMod]->hasData || m_pUpdateBufferQueue[bufferPositionMod]->hasSegData)
      {
        if (m_pUpdateBufferQueue[bufferPositionMod]->width != width ||
            m_pUpdateBufferQueue[bufferPositionMod]->height != height)
        {
          Log(DMDUtil_LogLevel_INFO, "ZeDMD: Change frame size from %dx%d to %dx%d", width, height,
              m_pUpdateBufferQueue[bufferPositionMod]->width, m_pUpdateBufferQueue[bufferPositionMod]->height);
          width = m_pUpdateBufferQueue[bufferPositionMod]->width;
          height = m_pUpdateBufferQueue[bufferPositionMod]->height;
          frameSize = width * height;
          // Activate the correct scaling mode.
          m_pZeDMD->SetFrameSize(width, height);
        }

        Log(DMDUtil_LogLevel_DEBUG, "ZeDMD: Render frame buffer position %d at real buffer postion %d", bufferPosition,
            bufferPositionMod);

        bool update = false;
        if (m_pUpdateBufferQueue[bufferPositionMod]->depth != 24)
        {
          update = UpdatePalette(palette, m_pUpdateBufferQueue[bufferPositionMod]->depth,
                                 m_pUpdateBufferQueue[bufferPositionMod]->r, m_pUpdateBufferQueue[bufferPositionMod]->g,
                                 m_pUpdateBufferQueue[bufferPositionMod]->b);
        }

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24)
        {
          // ZeDMD HD supports 256 * 64 pixels.
          uint8_t rgb24Data[256 * 64 * 3];

          AdjustRGB24Depth(m_pUpdateBufferQueue[bufferPositionMod]->data, rgb24Data, (size_t)width * height, palette,
                           m_pUpdateBufferQueue[bufferPositionMod]->depth);
          m_pZeDMD->RenderRgb888(rgb24Data);
        }
        else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16 ||
                 (m_pSerum && IsSerumV2Mode(m_pUpdateBufferQueue[bufferPositionMod]->mode)))
        {
          m_pZeDMD->RenderRgb565(m_pUpdateBufferQueue[bufferPositionMod]->segData);
        }
        else
        {
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1)
          {
            memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, PALETTE_SIZE);
            memcpy(indexBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, frameSize);
            update = true;
          }
          else if ((!m_pSerum && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
                   (showNotColorizedFrames && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::NotColorized))
          {
            memcpy(indexBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, frameSize);
            update = true;
          }
          else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::AlphaNumeric)
          {
            if (memcmp(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1)) != 0)
            {
              memcpy(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1));
              update = true;
            }

            if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2 &&
                memcmp(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2)) != 0)
            {
              memcpy(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2));
              update = true;
            }

            if (update)
            {
              if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2)
                m_pAlphaNumeric->Render(indexBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1,
                                        segData2);
              else
                m_pAlphaNumeric->Render(indexBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1);
            }
          }

          if (update)
          {
            uint16_t renderBuferPosition = 0;
            for (int i = 0; i < frameSize; i++)
            {
              int pos = indexBuffer[i] * 3;
              renderBuffer[renderBuferPosition++] = palette[pos];
              renderBuffer[renderBuferPosition++] = palette[pos + 1];
              renderBuffer[renderBuferPosition++] = palette[pos + 2];
            }
          }
        }

        if (update) m_pZeDMD->RenderRgb888(renderBuffer);
      }
    }
  }
}

void DMD::SerumThread()
{
  if (Config::GetInstance()->IsAltColor())
  {
    uint16_t bufferPosition = 0;
    uint32_t prevTriggerId = 0;
    char name[DMDUTIL_MAX_NAME_SIZE] = {0};
    char csvPath[DMDUTIL_MAX_PATH_SIZE + DMDUTIL_MAX_NAME_SIZE + DMDUTIL_MAX_NAME_SIZE + 10] = {0};
    uint32_t nextRotation = 0;
    Update* lastDmdUpdate = nullptr;
    uint8_t flags = 0;

    (void)m_dmdFrameReady.load(std::memory_order_acquire);
    (void)m_stopFlag.load(std::memory_order_acquire);

    Config* const pConfig = Config::GetInstance();
    bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();
    bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();

    int sceneFrameCount = 0;
    int sceneCurrentFrame = 0;
    int sceneDurationPerFrame = 0;
    bool sceneInterruptable = false;
    bool sceneStartImmediately = false;
    int sceneRepeatCount = 0;
    int sceneEndFrame = 0;
    uint32_t nextSceneFrame = 0;

    while (true)
    {
      if (m_stopFlag.load(std::memory_order_acquire))
      {
        if (m_pSerum)
        {
          Serum_Dispose();
        }

        return;
      }

      if (m_pSerum && nextRotation == 0 && sceneCurrentFrame >= sceneFrameCount)
      {
        uint16_t sceneId = m_pupSceneId.load(std::memory_order_relaxed);
        if (sceneId > 0)
        {
          if (m_pGenerator->getSceneInfo(sceneId, sceneFrameCount, sceneDurationPerFrame, sceneInterruptable,
                                         sceneStartImmediately, sceneRepeatCount, sceneEndFrame))
          {
            Log(DMDUtil_LogLevel_DEBUG, "Serum: PUP Scene ID %lu found in scenes, frame count=%d, duration=%dms",
                sceneId, sceneFrameCount, sceneDurationPerFrame);
            sceneCurrentFrame = 0;
            nextSceneFrame = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count() +
                             (sceneStartImmediately ? 0 : sceneDurationPerFrame);
          }
          // Reset the trigger after processing
          m_pupSceneId.store(0, std::memory_order_release);
        }
      }

      if (nextRotation == 0 && sceneCurrentFrame >= sceneFrameCount)
      {
        std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
        m_dmdCV.wait(
            sl, [&]()
            { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
        sl.unlock();
      }

      uint32_t now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();

      if (m_pSerum && sceneCurrentFrame < sceneFrameCount && nextSceneFrame <= now)
      {
        Update* sceneUpdate = new Update();
        if (m_pGenerator->generateFrame(prevTriggerId, sceneCurrentFrame++, sceneUpdate->data))
        {
          uint32_t result = Serum_Colorize(sceneUpdate->data);

          if (result != IDENTIFY_NO_FRAME)
          {
            Log(DMDUtil_LogLevel_DEBUG, "Serum: Got PUP scene %d, frame %d colorized", prevTriggerId,
                sceneCurrentFrame - 1);
            sceneUpdate->depth = m_pGenerator->getDepth();
            sceneUpdate->hasData = true;
            QueueSerumFrames(sceneUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES);
          }
        }
        nextSceneFrame = nextSceneFrame + sceneDurationPerFrame;
        delete sceneUpdate;

        // If the scene is finished.
        if (sceneCurrentFrame >= sceneFrameCount)
        {
          if (sceneRepeatCount > 1)
          {
            sceneCurrentFrame = 0;
            if (--sceneRepeatCount == 1) sceneRepeatCount = 0;
          }
          else if (sceneRepeatCount == 1)
          {
            // loop
            sceneCurrentFrame = 0;
          }
          else
          {
            sceneFrameCount = 0;
            if (lastDmdUpdate && sceneEndFrame >= 0)
            {
              uint32_t result = Serum_Colorize(lastDmdUpdate->data);
              if (result != IDENTIFY_NO_FRAME)
              {
                if (sceneEndFrame == 1)
                {
                  // Black frame.
                  memset(m_pSerum->palette, 0, PALETTE_SIZE);
                  if (m_pSerum->width32 > 0) memset(m_pSerum->frame32, 0, m_pSerum->width32 * 32 * sizeof(uint16_t));
                  if (m_pSerum->width64 > 0) memset(m_pSerum->frame64, 0, m_pSerum->width64 * 64 * sizeof(uint16_t));
                }
                while (std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count() < nextSceneFrame)
                {
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                QueueSerumFrames(lastDmdUpdate, result & 0x10000, result & 0x20000);
              }
            }
          }
        }
      }

      const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
      while (bufferPosition != updateBufferQueuePosition)
      {
        // Don't use GetNextBufferPosition() here, we need all frames for PUP triggers!
        ++bufferPosition;  // 65635 + 1 = 0
        uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

        if (m_pSerum && (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24 ||
                         m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16))
        {
          // DMDServer accepted a different connection, turn off Serum Colorization.
          Serum_Dispose();
          m_pSerum = nullptr;
          lastDmdUpdate = nullptr;
          m_pGenerator->Reset();
          sceneFrameCount = 0;
          continue;
        }

        if (m_pSerum && sceneCurrentFrame < sceneFrameCount && !sceneInterruptable) continue;

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data)
        {
          if (strcmp(m_romName, name) != 0)
          {
            strcpy(name, m_romName);

            if (m_pSerum)
            {
              Serum_Dispose();
              m_pSerum = nullptr;
              lastDmdUpdate = nullptr;
              m_pGenerator->Reset();
              sceneFrameCount = 0;
            }

            if (m_altColorPath[0] == '\0') strcpy(m_altColorPath, Config::GetInstance()->GetAltColorPath());
            flags = 0;
            // At the moment, ZeDMD HD and RGB24DMD are the only devices supporting 64P frames. Not requesting 64P
            // saves memory.
            if (m_pZeDMD)
            {
              if (m_pZeDMD->GetWidth() == 256)
                flags |= FLAG_REQUEST_64P_FRAMES;
              else
                flags |= FLAG_REQUEST_32P_FRAMES;
            }

            if (m_rgb24DMDs.size() > 0)
            {
              for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
              {
                if (pRGB24DMD->GetWidth() == 256)
                  flags |= FLAG_REQUEST_64P_FRAMES;
                else
                  flags |= FLAG_REQUEST_32P_FRAMES;
              }
            }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
            if (m_pPixelcadeDMD) flags |= FLAG_REQUEST_32P_FRAMES;
#endif

            m_pSerum = (name[0] != '\0') ? Serum_Load(m_altColorPath, m_romName, flags) : nullptr;
            if (m_pSerum)
            {
              Log(DMDUtil_LogLevel_INFO, "Serum: Loaded v%d colorization for %s", m_pSerum->SerumVersion, m_romName);

              Serum_SetIgnoreUnknownFramesTimeout(Config::GetInstance()->GetIgnoreUnknownFramesTimeout());
              Serum_SetMaximumUnknownFramesToSkip(Config::GetInstance()->GetMaximumUnknownFramesToSkip());

              if (m_dumpPath[strlen(m_altColorPath) - 1] == '/' || m_dumpPath[strlen(m_altColorPath) - 1] == '\\')
              {
                snprintf(csvPath, sizeof(csvPath), "%s%s/%s.pup.csv", m_altColorPath, m_romName, m_romName);
              }
              else
              {
                snprintf(csvPath, sizeof(csvPath), "%s/%s/%s.pup.csv", m_altColorPath, m_romName, m_romName);
              }
              Log(DMDUtil_LogLevel_INFO, "Serum: Check for PUP scenes for %s at %s", m_romName, csvPath);

              if (m_pGenerator->parseCSV(csvPath))
              {
                m_pGenerator->setDepth(m_pUpdateBufferQueue[bufferPositionMod]->depth);
                Log(DMDUtil_LogLevel_INFO, "Serum: Loaded PUP scenes for %s, bit depth %d", m_romName,
                    m_pUpdateBufferQueue[bufferPositionMod]->depth);
              }
            }
          }

          if (m_pSerum)
          {
            uint32_t result = Serum_Colorize(m_pUpdateBufferQueue[bufferPositionMod]->data);

            if (result != IDENTIFY_NO_FRAME)
            {
              // Log(DMDUtil_LogLevel_DEBUG, "Serum: frameID=%lu, rotation=%lu, flags=%lu", m_pSerum->frameID,
              // m_pSerum->rotationtimer, m_pSerum->flags);

              lastDmdUpdate = m_pUpdateBufferQueue[bufferPositionMod];

              if (result > 0 && ((result & 0xffff) < 2048))
                nextRotation = now + m_pSerum->rotationtimer;
              else
                nextRotation = 0;

              if (m_pSerum->triggerID < 0xffffffff)
              {
                if (m_pGenerator->getSceneInfo(m_pSerum->triggerID, sceneFrameCount, sceneDurationPerFrame,
                                               sceneInterruptable, sceneStartImmediately, sceneRepeatCount,
                                               sceneEndFrame))
                {
                  Log(DMDUtil_LogLevel_DEBUG, "Serum: trigger ID %lu found in scenes, frame count=%d, duration=%dms",
                      m_pSerum->triggerID, sceneFrameCount, sceneDurationPerFrame);
                  sceneCurrentFrame = 0;
                  if (sceneStartImmediately)
                  {
                    nextSceneFrame = now;
                  }
                  else
                  {
                    nextSceneFrame = now + sceneDurationPerFrame;
                    QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES);
                  }
                }
                else
                {
                  QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES);
                }

                HandleTrigger(m_pSerum->triggerID);
                prevTriggerId = m_pSerum->triggerID;
              }
              else
              {
                sceneFrameCount = 0;
                QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES);
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

              QueueUpdate(noSerumUpdate, false);
            }
          }
        }
      }

      if (m_pSerum)
      {
        if (sceneCurrentFrame < sceneFrameCount)
        {
          nextRotation = 0;
          continue;
        }

        if (nextRotation > 0 && m_pSerum->rotationtimer > 0 && lastDmdUpdate && now > nextRotation)
        {
          uint32_t result = Serum_Rotate();

          // Log(DMDUtil_LogLevel_DEBUG, "Serum: rotation=%lu, flags=%lu", m_pSerum->rotationtimer, result >> 16);

          QueueSerumFrames(lastDmdUpdate, result & 0x10000, result & 0x20000);

          if (result > 0 && ((result & 0xffff) < 2048))
          {
            nextRotation = now + m_pSerum->rotationtimer;
          }
          else
            nextRotation = 0;
        }
      }
    }
  }
}

void DMD::QueueSerumFrames(Update* dmdUpdate, bool render32, bool render64)
{
  if (!render32 && !render64) return;

  auto serumUpdate = std::make_shared<Update>();
  serumUpdate->hasData = true;
  serumUpdate->hasSegData = false;
  serumUpdate->hasSegData2 = false;

  if (m_pSerum->SerumVersion == SERUM_V1 && render32)
  {
    serumUpdate->mode = Mode::SerumV1;
    serumUpdate->depth = 6;
    serumUpdate->width = dmdUpdate->width;
    serumUpdate->height = dmdUpdate->height;
    memcpy(serumUpdate->data, m_pSerum->frame, (size_t)dmdUpdate->width * dmdUpdate->height);
    memcpy(serumUpdate->segData, m_pSerum->palette, PALETTE_SIZE);

    QueueUpdate(serumUpdate, false);
  }
  else if (m_pSerum->SerumVersion == SERUM_V2)
  {
    if (m_pSerum->width32 > 0 && m_pSerum->width64 == 0)
    {
      if (render32)
      {
        serumUpdate->mode = Mode::SerumV2_32;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width32;
        serumUpdate->height = 32;
        memcpy(serumUpdate->segData, m_pSerum->frame32, m_pSerum->width32 * 32 * sizeof(uint16_t));

        QueueUpdate(serumUpdate, false);
      }
    }
    else if (m_pSerum->width32 == 0 && m_pSerum->width64 > 0)
    {
      if (render64)
      {
        serumUpdate->mode = Mode::SerumV2_64;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width64;
        serumUpdate->height = 64;
        memcpy(serumUpdate->segData, m_pSerum->frame64, m_pSerum->width64 * 64 * sizeof(uint16_t));

        QueueUpdate(serumUpdate, false);
      }
    }
    else if (m_pSerum->width32 > 0 && m_pSerum->width64 > 0)
    {
      if (render32)
      {
        serumUpdate->mode = Mode::SerumV2_32_64;
        serumUpdate->depth = 24;
        serumUpdate->width = m_pSerum->width32;
        serumUpdate->height = 32;
        memcpy(serumUpdate->segData, m_pSerum->frame32, m_pSerum->width32 * 32 * sizeof(uint16_t));

        QueueUpdate(serumUpdate, false);
      }

      if (render64)
      {
        // We can't reuse the shared pointer from above because it might have been sent already.
        auto serumUpdateHD = std::make_shared<Update>();
        serumUpdateHD->hasData = true;
        serumUpdateHD->hasSegData = false;
        serumUpdateHD->hasSegData2 = false;
        serumUpdateHD->mode = Mode::SerumV2_64_32;
        serumUpdateHD->depth = 24;
        serumUpdateHD->width = m_pSerum->width64;
        serumUpdateHD->height = 64;
        memcpy(serumUpdateHD->segData, m_pSerum->frame64, m_pSerum->width64 * 64 * sizeof(uint16_t));

        QueueUpdate(serumUpdateHD, false);
      }
    }
  }
}

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))

void DMD::PixelcadeDMDThread()
{
  uint16_t bufferPosition = 0;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[PALETTE_SIZE] = {0};
  uint16_t rgb565Data[128 * 32] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pSerum && !IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames)) continue;

      if (m_pUpdateBufferQueue[bufferPositionMod]->hasData || m_pUpdateBufferQueue[bufferPositionMod]->hasSegData)
      {
        uint16_t width = m_pUpdateBufferQueue[bufferPositionMod]->width;
        uint16_t height = m_pUpdateBufferQueue[bufferPositionMod]->height;
        int length = (int)width * height;

        bool update = false;
        if (m_pUpdateBufferQueue[bufferPositionMod]->depth != 24)
        {
          update = UpdatePalette(palette, m_pUpdateBufferQueue[bufferPositionMod]->depth,
                                 m_pUpdateBufferQueue[bufferPositionMod]->r, m_pUpdateBufferQueue[bufferPositionMod]->g,
                                 m_pUpdateBufferQueue[bufferPositionMod]->b);
        }

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24)
        {
          uint8_t rgb24Data[256 * 64 * 3];
          AdjustRGB24Depth(m_pUpdateBufferQueue[bufferPositionMod]->data, rgb24Data, length, palette,
                           m_pUpdateBufferQueue[bufferPositionMod]->depth);

          uint8_t scaledBuffer[128 * 32 * 3];
          if (width == 128 && height == 32)
            memcpy(scaledBuffer, rgb24Data, 128 * 32 * 3);
          else if (width == 128 && height == 16)
            FrameUtil::Helper::Center(scaledBuffer, 128, 32, rgb24Data, 128, 16, 24);
          else if (height == 64)
            FrameUtil::Helper::ScaleDown(scaledBuffer, 128, 32, rgb24Data, width, 64, 24);
          else
            continue;

          for (int i = 0; i < 128 * 32; i++)
          {
            int pos = i * 3;
            uint32_t r = scaledBuffer[pos];
            uint32_t g = scaledBuffer[pos + 1];
            uint32_t b = scaledBuffer[pos + 2];

            rgb565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
          }
          update = true;
        }
        else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16)
        {
          if (width == 128 && height == 32)
            memcpy(rgb565Data, m_pUpdateBufferQueue[bufferPositionMod]->segData, 128 * 32 * 2);
          else if (width == 128 && height == 16)
            FrameUtil::Helper::Center((uint8_t*)rgb565Data, 128, 32,
                                      (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, 128, 16, 16);
          else if (height == 64)
            FrameUtil::Helper::ScaleDown((uint8_t*)rgb565Data, 128, 32,
                                         (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, width, 64, 16);
          else
            continue;

          update = true;
        }
        else if (IsSerumV2Mode(m_pUpdateBufferQueue[bufferPositionMod]->mode))
        {
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32 ||
              m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32_64)
            memcpy(rgb565Data, m_pUpdateBufferQueue[bufferPositionMod]->segData, 128 * 32 * 2);
          else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_64)
            FrameUtil::Helper::ScaleDown((uint8_t*)rgb565Data, 128, 32,
                                         (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, width, 64, 16);
          else
            continue;

          update = true;
        }
        else
        {
          uint8_t renderBuffer[256 * 64];

          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1)
          {
            memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, PALETTE_SIZE);
            memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
            update = true;
          }
          else if ((!m_pSerum && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
                   (showNotColorizedFrames && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::NotColorized))
          {
            memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
            update = true;
          }
          else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::AlphaNumeric)
          {
            if (memcmp(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1)) != 0)
            {
              memcpy(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1));
              update = true;
            }

            if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2 &&
                memcmp(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2)) != 0)
            {
              memcpy(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2));
              update = true;
            }

            if (update)
            {
              if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2)
                m_pAlphaNumeric->Render(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1,
                                        segData2);
              else
                m_pAlphaNumeric->Render(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1);
            }
          }

          if (update)
          {
            uint8_t scaledBuffer[128 * 32];
            if (width == 128 && height == 32)
              memcpy(scaledBuffer, renderBuffer, 128 * 32);
            else if (width == 128 && height == 16)
              FrameUtil::Helper::CenterIndexed(scaledBuffer, 128, 32, renderBuffer, 128, 16);
            else if (width == 192 && height == 64)
              FrameUtil::Helper::ScaleDownIndexed(scaledBuffer, 128, 32, renderBuffer, 192, 64);
            else if (width == 256 && height == 64)
              FrameUtil::Helper::ScaleDownIndexed(scaledBuffer, 128, 32, renderBuffer, 256, 64);
            else
              continue;

            for (int i = 0; i < 128 * 32; i++)
            {
              int pos = scaledBuffer[i] * 3;
              uint32_t r = palette[pos];
              uint32_t g = palette[pos + 1];
              uint32_t b = palette[pos + 2];

              rgb565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
            }
          }
        }

        if (update) m_pPixelcadeDMD->Update(rgb565Data);
      }
    }
  }
}
#endif

void DMD::LevelDMDThread()
{
  uint16_t bufferPosition = 0;
  uint8_t renderBuffer[256 * 64] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (!m_levelDMDs.empty() && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data &&
          m_pUpdateBufferQueue[bufferPositionMod]->hasData)
      {
        int length =
            (int)m_pUpdateBufferQueue[bufferPositionMod]->width * m_pUpdateBufferQueue[bufferPositionMod]->height;
        if (memcmp(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length) != 0)
        {
          memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
          for (LevelDMD* pLevelDMD : m_levelDMDs)
          {
            if (pLevelDMD->GetLength() == length)
              pLevelDMD->Update(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->depth);
          }
        }
      }
    }
  }
}

void DMD::RGB24DMDThread()
{
  uint16_t bufferPosition = 0;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[PALETTE_SIZE] = {0};
  uint8_t renderBuffer[256 * 64] = {0};
  uint8_t rgb24Data[256 * 64 * 3] = {0};
  uint8_t rgb24DataScaled[256 * 64 * 3] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pSerum && !IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames)) continue;

      if (!m_rgb24DMDs.empty() &&
          (m_pUpdateBufferQueue[bufferPositionMod]->hasData || m_pUpdateBufferQueue[bufferPositionMod]->hasSegData))
      {
        int length =
            (int)m_pUpdateBufferQueue[bufferPositionMod]->width * m_pUpdateBufferQueue[bufferPositionMod]->height;
        bool update = false;

        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB24)
        {
          if (memcmp(rgb24Data, m_pUpdateBufferQueue[bufferPositionMod]->data, length * 3) != 0)
          {
            if (m_pUpdateBufferQueue[bufferPositionMod]->depth != 24)
            {
              UpdatePalette(palette, m_pUpdateBufferQueue[bufferPositionMod]->depth,
                            m_pUpdateBufferQueue[bufferPositionMod]->r, m_pUpdateBufferQueue[bufferPositionMod]->g,
                            m_pUpdateBufferQueue[bufferPositionMod]->b);
            }

            AdjustRGB24Depth(m_pUpdateBufferQueue[bufferPositionMod]->data, rgb24Data, length, palette,
                             m_pUpdateBufferQueue[bufferPositionMod]->depth);

            for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
            {
              pRGB24DMD->Update(rgb24Data, m_pUpdateBufferQueue[bufferPositionMod]->width,
                                m_pUpdateBufferQueue[bufferPositionMod]->height);
            }
            // Reset renderBuffer in case the mode changes for the next frame to ensure that memcmp() will detect it.
            memset(renderBuffer, 0, sizeof(renderBuffer));
          }
        }
        else if (m_pUpdateBufferQueue[bufferPositionMod]->mode != Mode::RGB16 &&
                 !IsSerumV2Mode(m_pUpdateBufferQueue[bufferPositionMod]->mode))
        {
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1)
          {
            memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, PALETTE_SIZE);
            memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
            update = true;
          }
          else
          {
            update = UpdatePalette(
                palette, m_pUpdateBufferQueue[bufferPositionMod]->depth, m_pUpdateBufferQueue[bufferPositionMod]->r,
                m_pUpdateBufferQueue[bufferPositionMod]->g, m_pUpdateBufferQueue[bufferPositionMod]->b);

            if ((!m_pSerum && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
                (showNotColorizedFrames && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::NotColorized))
            {
              if (memcmp(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length) != 0)
              {
                memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
                update = true;
              }
            }
            else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::AlphaNumeric)
            {
              if (memcmp(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1)) != 0)
              {
                memcpy(segData1, m_pUpdateBufferQueue[bufferPositionMod]->segData, sizeof(segData1));
                update = true;
              }

              if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2 &&
                  memcmp(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2)) != 0)
              {
                memcpy(segData2, m_pUpdateBufferQueue[bufferPositionMod]->segData2, sizeof(segData2));
                update = true;
              }

              if (update)
              {
                if (m_pUpdateBufferQueue[bufferPositionMod]->hasSegData2)
                  m_pAlphaNumeric->Render(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1,
                                          segData2);
                else
                  m_pAlphaNumeric->Render(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->layout, segData1);
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
              pRGB24DMD->Update(rgb24Data, m_pUpdateBufferQueue[bufferPositionMod]->width,
                                m_pUpdateBufferQueue[bufferPositionMod]->height);
            }
          }
        }
        else
        {
          // Serum v2 or RGB16
          for (int i = 0; i < length; i++)
          {
            int pos = i * 3;
            rgb24Data[pos] = ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] >> 8) & 0xF8) |
                             ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] >> 13) & 0x07);
            rgb24Data[pos + 1] = ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] >> 3) & 0xFC) |
                                 ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] >> 9) & 0x03);
            rgb24Data[pos + 2] = ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] << 3) & 0xF8) |
                                 ((m_pUpdateBufferQueue[bufferPositionMod]->segData[i] >> 2) & 0x07);
          }

          for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
          {
            if (m_pSerum &&
                (!IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames) ||
                 (pRGB24DMD->GetWidth() == 256 &&
                  m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32_64) ||
                 (pRGB24DMD->GetWidth() < 256 && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_64_32)))
              continue;

            pRGB24DMD->Update(rgb24Data, m_pUpdateBufferQueue[bufferPositionMod]->width,
                              m_pUpdateBufferQueue[bufferPositionMod]->height);
          }
        }
      }
    }
  }
}

void DMD::ConsoleDMDThread()
{
  uint16_t bufferPosition = 0;
  uint8_t renderBuffer[256 * 64] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (!m_consoleDMDs.empty() && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data &&
          m_pUpdateBufferQueue[bufferPositionMod]->hasData)
      {
        int length =
            (int)m_pUpdateBufferQueue[bufferPositionMod]->width * m_pUpdateBufferQueue[bufferPositionMod]->height;
        if (memcmp(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length) != 0)
        {
          memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
          for (ConsoleDMD* pConsoleDMD : m_consoleDMDs)
          {
            pConsoleDMD->Render(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->width,
                                m_pUpdateBufferQueue[bufferPositionMod]->height,
                                m_pUpdateBufferQueue[bufferPositionMod]->depth);
          }
        }
      }
    }
  }
}

bool DMD::UpdatePalette(uint8_t* pPalette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b)
{
  if (depth != 2 && depth != 4) return false;
  uint8_t palette[192];

  const uint8_t colors = (depth == 2) ? 4 : 16;
  memcpy(palette, pPalette, colors * 3);
  uint8_t pos = 0;

  for (uint8_t i = 0; i < colors; i++)
  {
    float perc = FrameUtil::Helper::CalcBrightness((float)i / (float)(colors - 1));
    pPalette[pos++] = (uint8_t)((float)r * perc);
    pPalette[pos++] = (uint8_t)((float)g * perc);
    pPalette[pos++] = (uint8_t)((float)b * perc);
  }

  return (memcmp(pPalette, palette, colors * 3) != 0);
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
      pDstData[pos] = palette[pos2];
      pDstData[pos + 1] = palette[pos2 + 1];
      pDstData[pos + 2] = palette[pos2 + 2];
    }
  }
  else
  {
    memcpy(pDstData, pData, length * 3);
  }
}

void DMD::GenerateRandomSuffix(char* buffer, size_t length)
{
  static bool seedDone = false;
  if (!seedDone)
  {
    srand(static_cast<unsigned int>(time(nullptr)));
    seedDone = true;
  }

  const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  size_t charsetSize = sizeof(charset) - 1;  // exclude null terminator

  for (size_t i = 0; i < length; ++i)
  {
    buffer[i] = charset[rand() % charsetSize];
  }
  buffer[length] = '\0';
}

void DMD::DumpDMDTxtThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  uint16_t bufferPosition = 0;
  uint8_t renderBuffer[3][256 * 64] = {0};
  uint32_t passed[3] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;
  std::unordered_set<uint64_t> seenHashes;

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();
  bool filterTransitionalFrames = pConfig->IsFilterTransitionalFrames();

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      if (f)
      {
        fflush(f);
        fclose(f);
        f = nullptr;
      }
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      // Don't use GetNextBufferPosition() here, we need all frames!
      ++bufferPosition;  // 65635 + 1 = 0
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pUpdateBufferQueue[bufferPositionMod]->depth <= 4 && m_pUpdateBufferQueue[bufferPositionMod]->hasData &&
          ((m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data && !dumpNotColorizedFrames) ||
           (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::NotColorized && dumpNotColorizedFrames)))
      {
        bool update = false;
        if (strcmp(m_romName, name) != 0)
        {
          // New game ROM.
          start = std::chrono::steady_clock::now();
          if (f)
          {
            fclose(f);
            f = nullptr;
          }
          strcpy(name, m_romName);

          if (name[0] != '\0')
          {
            char filename[DMDUTIL_MAX_NAME_SIZE + 128 + 8 + 5];
            char suffix[9];  // 8 chars + null terminator
            GenerateRandomSuffix(suffix, 8);
            if (m_dumpPath[0] == '\0') strcpy(m_dumpPath, Config::GetInstance()->GetDumpPath());
            if (m_dumpPath[strlen(m_dumpPath) - 1] == '/' || m_dumpPath[strlen(m_dumpPath) - 1] == '\\')
            {
              snprintf(filename, sizeof(filename), "%s%s-%s.txt", m_dumpPath, name, suffix);
            }
            else
            {
              snprintf(filename, sizeof(filename), "%s/%s-%s.txt", m_dumpPath, name, suffix);
            }
            f = fopen(filename, "w");
            update = true;
            memset(renderBuffer, 0, 2 * 256 * 64);
            passed[0] = passed[1] = 0;
          }
        }

        if (name[0] != '\0')
        {
          int length =
              (int)m_pUpdateBufferQueue[bufferPositionMod]->width * m_pUpdateBufferQueue[bufferPositionMod]->height;
          if (update || (memcmp(renderBuffer[1], m_pUpdateBufferQueue[bufferPositionMod]->data, length) != 0))
          {
            passed[2] = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count());
            memcpy(renderBuffer[2], m_pUpdateBufferQueue[bufferPositionMod]->data, length);

            if (filterTransitionalFrames && m_pUpdateBufferQueue[bufferPositionMod]->depth == 2 &&
                (passed[2] - passed[1]) < DMDUTIL_MAX_TRANSITIONAL_FRAME_DURATION)
            {
              int i = 0;
              while (i < length &&
                     ((renderBuffer[0][i] == 2) ||
                      ((renderBuffer[0][i] == 3) || (renderBuffer[2][i] > 1)) == (renderBuffer[1][i] > 0)))
              {
                i++;
              }
              if (i == length)
              {
                Log(DMDUtil_LogLevel_DEBUG, "DumpDMDTxt: skip transitional frame");

                // renderBuffer[1] is a transitional frame, delete it.
                memcpy(renderBuffer[1], renderBuffer[2], length);
                passed[1] += passed[2];
                continue;
              }
            }

            if (f)
            {
              if (passed[0] > 0)
              {
                bool dump = true;

                if (dumpNotColorizedFrames)
                {
                  uint64_t hash = komihash(renderBuffer[0], length, 0);
                  if (seenHashes.find(hash) == seenHashes.end())
                  {
                    seenHashes.insert(hash);
                  }
                  else
                  {
                    Log(DMDUtil_LogLevel_DEBUG, "DumpDMDTxt: skip duplicate frame");
                    dump = false;
                  }
                }

                if (dump)
                {
                  fprintf(f, "0x%08x\r\n", passed[0]);
                  for (int y = 0; y < m_pUpdateBufferQueue[bufferPositionMod]->height; y++)
                  {
                    for (int x = 0; x < m_pUpdateBufferQueue[bufferPositionMod]->width; x++)
                    {
                      fprintf(f, "%x", renderBuffer[0][y * m_pUpdateBufferQueue[bufferPositionMod]->width + x]);
                    }
                    fprintf(f, "\r\n");
                  }
                  fprintf(f, "\r\n");
                }
              }
            }
            memcpy(renderBuffer[0], renderBuffer[1], length);
            passed[0] = passed[1];
            memcpy(renderBuffer[1], renderBuffer[2], length);
            passed[1] = passed[2];
          }
        }
      }
    }
  }
}

void DMD::DumpDMDRawThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  uint16_t bufferPosition = 0;
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      if (f)
      {
        fflush(f);
        fclose(f);
        f = nullptr;
      }
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      // Don't use GetNextBufferPosition() here, we need all frames!
      ++bufferPosition;  // 65635 + 1 = 0
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (m_pUpdateBufferQueue[bufferPositionMod]->hasData || m_pUpdateBufferQueue[bufferPositionMod]->hasSegData)
      {
        if (strcmp(m_romName, name) != 0)
        {
          // New game ROM.
          start = std::chrono::steady_clock::now();
          if (f)
          {
            fclose(f);
            f = nullptr;
          }
          strcpy(name, m_romName);

          if (name[0] != '\0')
          {
            char filename[128];
            snprintf(filename, DMDUTIL_MAX_NAME_SIZE + 5, "%s.raw", name);
            f = fopen(filename, "ab");
          }
        }

        if (name[0] != '\0')
        {
          if (f)
          {
            auto current =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            fwrite(&current, 4, 1, f);

            uint32_t size = sizeof(m_pUpdateBufferQueue[bufferPositionMod]);
            fwrite(&size, 4, 1, f);

            fwrite(m_pUpdateBufferQueue[bufferPositionMod], 1, size, f);
          }
        }
      }
    }
  }
}

void DMD::PupDMDThread()
{
  uint16_t bufferPosition = 0;
  uint8_t renderBuffer[256 * 64] = {0};
  uint8_t palette[192] = {0};
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};

  (void)m_dmdFrameReady.load(std::memory_order_acquire);
  (void)m_stopFlag.load(std::memory_order_acquire);

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    m_dmdCV.wait(
        sl, [&]()
        { return m_dmdFrameReady.load(std::memory_order_relaxed) || m_stopFlag.load(std::memory_order_relaxed); });
    sl.unlock();
    if (m_stopFlag.load(std::memory_order_acquire))
    {
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      // Don't use GetNextBufferPosition() here, we need all frames!
      ++bufferPosition;  // 65635 + 1 = 0
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if (strcmp(m_romName, name) != 0)
      {
        strcpy(name, m_romName);

        if (Config::GetInstance()->IsPUPCapture())
        {
          if (m_pPUPDMD)
          {
            delete (m_pPUPDMD);
            m_pPUPDMD = nullptr;
          }

          if (name[0] != '\0')
          {
            if (m_pupVideosPath[0] == '\0') strcpy(m_pupVideosPath, Config::GetInstance()->GetPUPVideosPath());
            m_pPUPDMD = new PUPDMD::DMD();
            m_pPUPDMD->SetLogCallback(PUPDMDLogCallback, nullptr);

            if (!m_pPUPDMD->Load(m_pupVideosPath, m_romName, m_pUpdateBufferQueue[bufferPositionMod]->depth))
            {
              delete (m_pPUPDMD);
              m_pPUPDMD = nullptr;
            }
          }
        }
      }

      if (m_pPUPDMD && m_pUpdateBufferQueue[bufferPositionMod]->hasData &&
          m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data &&
          m_pUpdateBufferQueue[bufferPositionMod]->depth != 24)
      {
        uint16_t width = m_pUpdateBufferQueue[bufferPositionMod]->width;
        uint16_t height = m_pUpdateBufferQueue[bufferPositionMod]->height;
        int length = (int)width * height;

        if (memcmp(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length) != 0)
        {
          memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
          uint8_t depth = m_pUpdateBufferQueue[bufferPositionMod]->depth;

          uint8_t scaledBuffer[128 * 32];
          if (width == 128 && height == 32)
            memcpy(scaledBuffer, renderBuffer, 128 * 32);
          else if (width == 128 && height == 16)
            FrameUtil::Helper::CenterIndexed(scaledBuffer, 128, 32, renderBuffer, 128, 16);
          else if (width == 192 && height == 64)
            FrameUtil::Helper::ScaleDownPUP(scaledBuffer, 128, 32, renderBuffer, 192, 64);
          else
            return;

          uint16_t triggerID = 0;
          if (Config::GetInstance()->IsPUPExactColorMatch())
          {
            triggerID = m_pPUPDMD->MatchIndexed(scaledBuffer, width, height);
          }
          else
          {
            // apply a standard orange palette
            UpdatePalette(palette, depth, 255, 69, 0);

            uint8_t* pFrame = (uint8_t*)malloc(length * 3);
            for (uint16_t i = 0; i < length; i++)
            {
              uint16_t pos = scaledBuffer[i] * 3;
              memcpy(&pFrame[i * 3], &palette[pos], 3);
            }
            triggerID = m_pPUPDMD->Match(pFrame, width, height, false);
            free(pFrame);
          }

          if (triggerID > 0) HandleTrigger(triggerID);
        }
      }
    }
  }
}

void DMD::HandleTrigger(uint16_t id)
{
  static Config* pConfig = Config::GetInstance();

  Log(DMDUtil_LogLevel_DEBUG, "HandleTrigger: id=D%d", id);

  DMDUtil_PUPTriggerCallbackContext callbackContext = pConfig->GetPUPTriggerCallbackContext();
  if (callbackContext.callback != nullptr)
  {
    (*callbackContext.callback)(id, callbackContext.pUserData);
  }
}

void DMD::SetPUPTrigger(const char source, const uint16_t event, const uint8_t value)
{
  if (m_pSerum)
  {
    uint16_t id = m_pGenerator->getSceneId(source, event, value);
    if (id > 0)
    {
      while (m_pupSceneId.load(std::memory_order_acquire) != 0)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      m_pupSceneId.store(id, std::memory_order_release);
    }
  }
}

}  // namespace DMDUtil
