#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/ConsoleDMD.h"
#include "DMDUtil/LevelDMD.h"
#include "DMDUtil/RGB24DMD.h"

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

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
#include "pin2dmd.h"
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "AlphaNumeric.h"
#include "FrameUtil.h"
#include "Logger.h"
#include "TimeUtils.h"
#include "ZeDMD.h"
#include "komihash/komihash.h"
#include "pupdmd.h"
#include "serum-decode.h"
#include "serum.h"
#include "vni.h"
#include "sockpp/tcp_connector.h"

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

size_t PaletteBytesForDepth(uint8_t depth)
{
  if (depth > 8)
  {
    return 0;
  }
  return (static_cast<size_t>(1u) << depth) * 3u;
}
}  // namespace

namespace DMDUtil
{

void SERUM_CALLBACK Serum_LogCallback(const char* format, va_list args, const void* pUserData)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);

  Log(DMDUtil_LogLevel_INFO, "%s", buffer);
}

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
  m_updateBuffered = std::make_shared<Update>();

  m_pAlphaNumeric = new AlphaNumeric();
  m_pSerum = nullptr;
  m_pVni = nullptr;
  m_pZeDMD = nullptr;
  m_pPUPDMD = nullptr;

  m_pZeDMDThread = nullptr;
  m_pLevelDMDThread = nullptr;
  m_pRGB24DMDThread = nullptr;
  m_pConsoleDMDThread = nullptr;
  m_pDumpDMDTxtThread = nullptr;
  m_pDumpDMDRawThread = nullptr;
  m_pDumpDMDRgb565Thread = nullptr;
  m_pDumpDMDRgb888Thread = nullptr;
#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  m_pPixelcadeDMD = nullptr;
  m_pPixelcadeDMDThread = nullptr;
#endif

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
  m_pPin2DMDThread = nullptr;
  m_pin2dmdConnected = false;
  m_pin2dmdWidth = 0;
  m_pin2dmdHeight = 0;
#endif

  m_pDmdFrameThread = new std::thread(&DMD::DmdFrameThread, this);
  m_pPupDMDThread = new std::thread(&DMD::PupDMDThread, this);
  m_pSerumThread = new std::thread(&DMD::SerumThread, this);
  m_pVniThread = new std::thread(&DMD::VniThread, this);
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

  if (m_pDumpDMDRgb565Thread)
  {
    m_pDumpDMDRgb565Thread->join();
    delete m_pDumpDMDRgb565Thread;
    m_pDumpDMDRgb565Thread = nullptr;
  }

  if (m_pDumpDMDRgb888Thread)
  {
    m_pDumpDMDRgb888Thread->join();
    delete m_pDumpDMDRgb888Thread;
    m_pDumpDMDRgb888Thread = nullptr;
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

  if (m_pVniThread)
  {
    m_pVniThread->join();
    delete m_pVniThread;
    m_pVniThread = nullptr;
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

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
  if (m_pPin2DMDThread)
  {
    m_pPin2DMDThread->join();
    delete m_pPin2DMDThread;
    m_pPin2DMDThread = nullptr;
  }
#endif
  delete m_pAlphaNumeric;
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

  for (uint8_t i = 0; i < DMDUTIL_FRAME_BUFFER_SIZE; i++)
  {
    delete m_pUpdateBufferQueue[i];
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
  if (m_pZeDMD != nullptr || m_rgb24DMDs.size() > 0)
  {
    return true;
  }

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
  if (m_pPixelcadeDMD != nullptr) return true;
#endif

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
  if (m_pin2dmdConnected) return true;
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

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
  if (m_pin2dmdConnected && m_pin2dmdWidth == 256) return true;
#endif

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

void DMD::DumpDMDRgb565()
{
  if (!m_pDumpDMDRgb565Thread)
  {
    m_pDumpDMDRgb565Thread = new std::thread(&DMD::DumpDMDRgb565Thread, this);
  }
}

void DMD::DumpDMDRgb888()
{
  if (!m_pDumpDMDRgb888Thread)
  {
    m_pDumpDMDRgb888Thread = new std::thread(&DMD::DumpDMDRgb888Thread, this);
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
          bool openSpi = false;

          if (pConfig->IsZeDMD() || pConfig->IsZeDMDWiFiEnabled() || pConfig->IsZeDMDSpiEnabled())
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

          if (pConfig->IsZeDMDSpiEnabled())
          {
            Log(DMDUtil_LogLevel_INFO, "ZeDMD SPI: try to open with speed=%d, framePause=%d, width=%d, height=%d",
                pConfig->GetZeDMDSpiSpeed(), pConfig->GetZeDMDSpiFramePause(), pConfig->GetZeDMDWidth(),
                pConfig->GetZeDMDHeight());
            if ((openSpi = pZeDMD->OpenSpi(pConfig->GetZeDMDSpiSpeed(), pConfig->GetZeDMDSpiFramePause(),
                                           pConfig->GetZeDMDWidth(), pConfig->GetZeDMDHeight())))
            {
              Log(DMDUtil_LogLevel_INFO, "ZeDMD SPI: speed=%d, framePause=%d, width=%d, height=%d",
                  pConfig->GetZeDMDSpiSpeed(), pConfig->GetZeDMDSpiFramePause(), pZeDMD->GetWidth(),
                  pZeDMD->GetHeight());
            }
            else
            {
              Log(DMDUtil_LogLevel_ERROR, "ZeDMD SPI failed");
            }
          }

          if (openSerial || openWiFi || openSpi)
          {
            if (pConfig->IsZeDMDDebug())
            {
              pZeDMD->EnableDebug();
              pZeDMD->EnableVerbose();
            }
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
            pPixelcadeDMD = PixelcadeDMD::Connect(pConfig->GetPixelcadeDevice());
            if (pPixelcadeDMD) m_pPixelcadeDMDThread = new std::thread(&DMD::PixelcadeDMDThread, this);
          }

          m_pPixelcadeDMD = pPixelcadeDMD;
#endif

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
          m_pin2dmdConnected = false;
          m_pin2dmdWidth = 0;
          m_pin2dmdHeight = 0;

          if (pConfig->IsPin2DMD())
          {
            int pin2dmdInit = Pin2dmdInit();
            if (pin2dmdInit > 0)
            {
              m_pin2dmdWidth = Pin2dmdGetWidth();
              m_pin2dmdHeight = Pin2dmdGetHeight();
              if (m_pin2dmdWidth > 0 && m_pin2dmdHeight > 0)
              {
                m_pin2dmdConnected = true;
                m_pPin2DMDThread = new std::thread(&DMD::Pin2DMDThread, this);
              }
            }
          }
#endif

          m_finding = false;
        })
        .detach();
  }
}

uint16_t DMD::GetNextBufferQueuePosition(uint16_t bufferPosition, const uint16_t updateBufferQueuePosition)
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
    if ((65535 - bufferPosition + updateBufferQueuePosition) > DMDUTIL_MAX_FRAMES_BEHIND)
      // Too many frames behind, skip a lot, if the result is negative, it's fine, it
      // will wrap around
      return updateBufferQueuePosition - DMDUTIL_MIN_FRAMES_BEHIND;
    else if ((65535 - bufferPosition + updateBufferQueuePosition) > (DMDUTIL_MAX_FRAMES_BEHIND / 2))
      return ++bufferPosition;  // Skip one frame to avoid too many frames behind
  }

  return bufferPosition;
}

void DMD::DmdFrameThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  uint16_t bufferPosition = 0;

  (void)m_stopFlag.load(std::memory_order_acquire);

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

    bufferPosition = m_updateBufferQueuePosition.load(std::memory_order_relaxed);

    if (strcmp(m_romName, name) != 0)
    {
      strcpy(name, m_romName);

      // In case of a new ROM, try to disconnect the other clients.
      if (m_pDMDServerConnector) m_dmdServerDisconnectOthers = true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

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
  uint8_t palette[256 * 3] = {0};
  uint8_t indexBuffer[256 * 64] = {0};
  uint8_t renderBuffer[256 * 64 * 3] = {0};

  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

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
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      uint16_t nextBufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      if (nextBufferPosition > bufferPosition && (nextBufferPosition - bufferPosition) > 1)
      {
        Log(DMDUtil_LogLevel_INFO, "ZeDMD: Skipping %d frame(s) from position %d to %d",
            nextBufferPosition - bufferPosition - 1, bufferPosition, nextBufferPosition);
      }
      else if (nextBufferPosition < bufferPosition && (65535 - bufferPosition + nextBufferPosition) > 1)
      {
        Log(DMDUtil_LogLevel_INFO, "ZeDMD: Skipping frames from position %d to %d (overflow)", bufferPosition,
            nextBufferPosition);
      }
      bufferPosition = nextBufferPosition;
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if ((m_pSerum || m_pVni) &&
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

        Log(DMDUtil_LogLevel_DEBUG, "ZeDMD: Render frame buffer position %d at real buffer position %d", bufferPosition,
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
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1 ||
              m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Vni)
          {
            size_t paletteBytes = PaletteBytesForDepth((uint8_t)m_pUpdateBufferQueue[bufferPositionMod]->depth);
            if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
            {
              memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, paletteBytes);
            }
            memcpy(indexBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, frameSize);
            update = true;
          }
          else if ((!(m_pSerum || m_pVni) && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
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
  Config* const pConfig = Config::GetInstance();

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
        }

        return;
      }

      if (m_pSerum && nextRotation == 0)
      {
        uint16_t sceneId = m_pupSceneId.load(std::memory_order_relaxed);
        if (sceneId > 0)
        {
          // @todo

          // Reset the trigger after processing
          m_pupSceneId.store(0, std::memory_order_release);
        }
      }

      if (nextRotation == 0)
      {
        std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
        m_dmdCV.wait(sl,
                     [&]()
                     {
                       return m_stopFlag.load(std::memory_order_relaxed) ||
                              (m_updateBufferQueuePosition.load(std::memory_order_relaxed) != bufferPosition);
                     });
        sl.unlock();
      }

      uint32_t now = GetMonotonicTimeMs();

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
          strcpy(name, "");
          QueueBuffer();
          continue;
        }

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
            }

            if (m_altColorPath[0] == '\0') strcpy(m_altColorPath, Config::GetInstance()->GetAltColorPath());
            flags = 0;

            // At the moment, ZeDMD HD and RGB24DMD are the only devices supporting 64P frames. Not requesting 64P
            // saves memory.
            if (m_pZeDMD)
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

#if !(                                                                                                                \
    (defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || \
    defined(__ANDROID__))
            if (m_pPixelcadeDMD) flags |= FLAG_REQUEST_32P_FRAMES;
#endif

#if defined(DMDUTIL_ENABLE_PIN2DMD) &&                                                                                \
    !(                                                                                                                \
        (defined(__APPLE__) &&                                                                                        \
         ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) ||                    \
        defined(__ANDROID__))
            if (m_pin2dmdConnected)
            {
              if (m_pin2dmdHeight == 64)
                flags |= FLAG_REQUEST_64P_FRAMES;
              else
                flags |= FLAG_REQUEST_32P_FRAMES;
            }
#endif

            if (!flags) flags |= FLAG_REQUEST_32P_FRAMES;

            m_pSerum = (name[0] != '\0') ? Serum_Load(m_altColorPath, m_romName, flags) : nullptr;
            if (m_pSerum)
            {
              Log(DMDUtil_LogLevel_INFO, "Serum: Loaded v%d colorization for %s", m_pSerum->SerumVersion, m_romName);

              Serum_SetIgnoreUnknownFramesTimeout(Config::GetInstance()->GetIgnoreUnknownFramesTimeout());
              Serum_SetMaximumUnknownFramesToSkip(Config::GetInstance()->GetMaximumUnknownFramesToSkip());
            }
          }

          if (m_pSerum)
          {
            uint32_t result = Serum_Colorize(m_pUpdateBufferQueue[bufferPositionMod]->data);

            if (result != IDENTIFY_NO_FRAME && result != IDENTIFY_SAME_FRAME)
            {
              // Log(DMDUtil_LogLevel_DEBUG, "Serum: frameID=%lu, rotation=%lu, flags=%lu", m_pSerum->frameID,
              // m_pSerum->rotationtimer, m_pSerum->flags);

              lastDmdUpdate = m_pUpdateBufferQueue[bufferPositionMod];

              QueueSerumFrames(lastDmdUpdate, flags & FLAG_REQUEST_32P_FRAMES, flags & FLAG_REQUEST_64P_FRAMES);

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

              QueueUpdate(noSerumUpdate, false);
            }
          }
        }
      }

      if (m_pSerum)
      {
        if (nextRotation > 0 && m_pSerum->rotationtimer > 0 && lastDmdUpdate && now >= nextRotation)
        {
          uint32_t result = Serum_Rotate();

          Log(DMDUtil_LogLevel_DEBUG, "Serum: rotation=%lu, flags=%lu", m_pSerum->rotationtimer, result >> 16);

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

void DMD::VniThread()
{
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
            m_pVni = Vni_LoadFromPaths(palPath.empty() ? nullptr : palPath.c_str(),
                                       vniPath.empty() ? nullptr : vniPath.c_str(),
                                       pacPath.empty() ? nullptr : pacPath.c_str(),
                                       (vniKey && vniKey[0] != '\0') ? vniKey : nullptr);
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

                QueueUpdate(vniUpdate, false);
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

            QueueUpdate(noVniUpdate, false);
          }
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

#if defined(DMDUTIL_ENABLE_PIN2DMD)
void DMD::Pin2DMDThread()
{
  uint16_t bufferPosition = 0;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[256 * 3] = {0};
  uint8_t renderBuffer[256 * 64] = {0};

  const int targetWidth = m_pin2dmdWidth;
  const int targetHeight = m_pin2dmdHeight;
  const int targetLength = targetWidth * targetHeight;
  const int maxSourceLength = 256 * 64;
  uint8_t* rgb24Data = new uint8_t[maxSourceLength * 3];
  uint8_t* scaledBuffer = new uint8_t[targetLength * 3];
  constexpr int kMaxTempWidth = 384;
  constexpr int kMaxTempHeight = 128;
  uint8_t* tempBuffer = new uint8_t[kMaxTempWidth * kMaxTempHeight * 3];

  memset(rgb24Data, 0, maxSourceLength * 3);
  memset(scaledBuffer, 0, targetLength * 3);

  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

  auto scaleToTarget = [&](const uint8_t* src, uint16_t width, uint16_t height, uint8_t* dst) -> bool
  {
    if (width == targetWidth && height == targetHeight)
    {
      memcpy(dst, src, targetLength * 3);
      return true;
    }

    if (width == targetWidth && height == 16)
    {
      FrameUtil::Helper::Center(dst, targetWidth, targetHeight, src, targetWidth, 16, 24);
      return true;
    }

    if (height == 64 && targetHeight == 32)
    {
      FrameUtil::Helper::ScaleDown(dst, targetWidth, targetHeight, src, width, 64, 24);
      return true;
    }

    if (height == 32 && targetHeight == 64)
    {
      const int upWidth = width * 2;
      const int upHeight = height * 2;
      if (upWidth == targetWidth && upHeight == targetHeight)
      {
        FrameUtil::Helper::ScaleUp(dst, src, width, height, 24);
        return true;
      }
      if (upWidth <= kMaxTempWidth && upHeight <= kMaxTempHeight)
      {
        FrameUtil::Helper::ScaleUp(tempBuffer, src, width, height, 24);
        FrameUtil::Helper::ScaleDown(dst, targetWidth, targetHeight, tempBuffer, upWidth, upHeight, 24);
        return true;
      }
      return false;
    }

    if (height == 64 && targetHeight == 64)
    {
      if (width > targetWidth)
      {
        FrameUtil::Helper::ScaleDown(dst, targetWidth, targetHeight, src, width, height, 24);
        return true;
      }
      if (width < targetWidth)
      {
        const int upWidth = width * 2;
        const int upHeight = height * 2;
        if (upWidth <= kMaxTempWidth && upHeight <= kMaxTempHeight)
        {
          FrameUtil::Helper::ScaleUp(tempBuffer, src, width, height, 24);
          FrameUtil::Helper::ScaleDown(dst, targetWidth, targetHeight, tempBuffer, upWidth, upHeight, 24);
          return true;
        }
      }
    }

    return false;
  };

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
      delete[] rgb24Data;
      delete[] scaledBuffer;
      delete[] tempBuffer;
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if ((m_pSerum || m_pVni) &&
          !IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames))
        continue;

      if (!(m_pUpdateBufferQueue[bufferPositionMod]->hasData || m_pUpdateBufferQueue[bufferPositionMod]->hasSegData))
        continue;

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
        AdjustRGB24Depth(m_pUpdateBufferQueue[bufferPositionMod]->data, rgb24Data, length, palette,
                         m_pUpdateBufferQueue[bufferPositionMod]->depth);
        update = true;
      }
      else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16 ||
               IsSerumV2Mode(m_pUpdateBufferQueue[bufferPositionMod]->mode))
      {
        const uint16_t* src = m_pUpdateBufferQueue[bufferPositionMod]->segData;
        for (int i = 0; i < length; i++)
        {
          uint16_t value = src[i];
          uint8_t r = (uint8_t)((value >> 11) & 0x1F);
          uint8_t g = (uint8_t)((value >> 5) & 0x3F);
          uint8_t b = (uint8_t)(value & 0x1F);
          rgb24Data[i * 3] = (uint8_t)((r << 3) | (r >> 2));
          rgb24Data[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
          rgb24Data[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
        }
        update = true;
      }
      else
      {
        if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1 ||
            m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Vni)
        {
          size_t paletteBytes = PaletteBytesForDepth((uint8_t)m_pUpdateBufferQueue[bufferPositionMod]->depth);
          if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
          {
            memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, paletteBytes);
          }
          memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
          update = true;
        }
        else if ((!(m_pSerum || m_pVni) && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
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
          FrameUtil::Helper::ConvertToRgb24(rgb24Data, renderBuffer, length, palette);
        }
      }

      if (update && scaleToTarget(rgb24Data, width, height, scaledBuffer))
      {
        Pin2dmdRenderRaw(targetWidth, targetHeight, scaledBuffer, 1);
      }
    }
  }
}
#endif

void DMD::PixelcadeDMDThread()
{
  uint16_t bufferPosition = 0;
  uint16_t segData1[128] = {0};
  uint16_t segData2[128] = {0};
  uint8_t palette[256 * 3] = {0};

  const int targetWidth = m_pPixelcadeDMD->GetWidth();
  const int targetHeight = m_pPixelcadeDMD->GetHeight();
  const int targetLength = targetWidth * targetHeight;
  uint16_t* rgb565Data = new uint16_t[targetLength];
  memset(rgb565Data, 0, targetLength * sizeof(uint16_t));

  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

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
      delete[] rgb565Data;
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if ((m_pSerum || m_pVni) &&
          !IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames))
        continue;

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

          uint8_t* scaledBuffer = new uint8_t[targetLength * 3];
          if (width == targetWidth && height == targetHeight)
            memcpy(scaledBuffer, rgb24Data, targetLength * 3);
          else if (width == targetWidth && height == 16)
            FrameUtil::Helper::Center(scaledBuffer, targetWidth, targetHeight, rgb24Data, targetWidth, 16, 24);
          else if (height == 64)
            FrameUtil::Helper::ScaleDown(scaledBuffer, targetWidth, targetHeight, rgb24Data, width, 64, 24);
          else
            continue;

          if (m_pPixelcadeDMD->GetIsV2())
          {
            m_pPixelcadeDMD->UpdateRGB24(scaledBuffer);
          }
          else
          {
            for (int i = 0; i < targetLength; i++)
            {
              int pos = i * 3;
              uint32_t r = scaledBuffer[pos];
              uint32_t g = scaledBuffer[pos + 1];
              uint32_t b = scaledBuffer[pos + 2];

              rgb565Data[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
            }
            update = true;
          }

          delete[] scaledBuffer;
        }
        else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::RGB16)
        {
          if (width == targetWidth && height == targetHeight)
            memcpy(rgb565Data, m_pUpdateBufferQueue[bufferPositionMod]->segData, targetLength * 2);
          else if (width == targetWidth && height == 16)
            FrameUtil::Helper::Center((uint8_t*)rgb565Data, targetWidth, targetHeight,
                                      (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, targetWidth, 16, 16);
          else if (height == 64)
            FrameUtil::Helper::ScaleDown((uint8_t*)rgb565Data, targetWidth, targetHeight,
                                         (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, width, 64, 16);
          else
            continue;

          update = true;
        }
        else if (IsSerumV2Mode(m_pUpdateBufferQueue[bufferPositionMod]->mode))
        {
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32 ||
              m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_32_64)
            memcpy(rgb565Data, m_pUpdateBufferQueue[bufferPositionMod]->segData, targetLength * 2);
          else if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV2_64)
            FrameUtil::Helper::ScaleDown((uint8_t*)rgb565Data, targetWidth, targetHeight,
                                         (uint8_t*)m_pUpdateBufferQueue[bufferPositionMod]->segData, width, 64, 16);
          else
            continue;

          update = true;
        }
        else
        {
          uint8_t renderBuffer[256 * 64];

          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1 ||
              m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Vni)
          {
            size_t paletteBytes = PaletteBytesForDepth((uint8_t)m_pUpdateBufferQueue[bufferPositionMod]->depth);
            if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
            {
              memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, paletteBytes);
            }
            memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
            update = true;
          }
          else if ((!(m_pSerum || m_pVni) && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
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
            if (width == targetWidth && height == targetHeight)
              memcpy(scaledBuffer, renderBuffer, targetLength);
            else if (width == targetWidth && height == 16)
              FrameUtil::Helper::CenterIndexed(scaledBuffer, targetWidth, targetHeight, renderBuffer, targetWidth, 16);
            else if (width == 192 && height == 64)
              FrameUtil::Helper::ScaleDownIndexed(scaledBuffer, targetWidth, targetHeight, renderBuffer, 192, 64);
            else if (width == 256 && height == 64)
              FrameUtil::Helper::ScaleDownIndexed(scaledBuffer, targetWidth, targetHeight, renderBuffer, 256, 64);
            else
              continue;

            for (int i = 0; i < targetLength; i++)
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

  (void)m_stopFlag.load(std::memory_order_acquire);

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
  uint8_t palette[256 * 3] = {0};
  uint8_t renderBuffer[256 * 64] = {0};
  uint8_t rgb24Data[256 * 64 * 3] = {0};
  uint8_t rgb24DataScaled[256 * 64 * 3] = {0};

  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool showNotColorizedFrames = pConfig->IsShowNotColorizedFrames();

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
      return;
    }

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      bufferPosition = GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      uint8_t bufferPositionMod = bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE;

      if ((m_pSerum || m_pVni) &&
          !IsSerumMode(m_pUpdateBufferQueue[bufferPositionMod]->mode, showNotColorizedFrames))
        continue;

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
          if (m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::SerumV1 ||
              m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Vni)
          {
            size_t paletteBytes = PaletteBytesForDepth((uint8_t)m_pUpdateBufferQueue[bufferPositionMod]->depth);
            if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
            {
              memcpy(palette, m_pUpdateBufferQueue[bufferPositionMod]->segData, paletteBytes);
            }
            memcpy(renderBuffer, m_pUpdateBufferQueue[bufferPositionMod]->data, length);
            update = true;
          }
          else
          {
            update = UpdatePalette(
                palette, m_pUpdateBufferQueue[bufferPositionMod]->depth, m_pUpdateBufferQueue[bufferPositionMod]->r,
                m_pUpdateBufferQueue[bufferPositionMod]->g, m_pUpdateBufferQueue[bufferPositionMod]->b);

            if ((!(m_pSerum || m_pVni) && m_pUpdateBufferQueue[bufferPositionMod]->mode == Mode::Data) ||
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
            if ((m_pSerum || m_pVni) &&
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

  (void)m_stopFlag.load(std::memory_order_acquire);

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

bool DMD::GetDumpSuffix(const char* romName, char* outSuffix, size_t outSize)
{
  if (!romName || romName[0] == '\0' || !outSuffix || outSize == 0)
  {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_dumpSuffixMutex);
  if (!m_dumpSuffixValid || strcmp(romName, m_dumpSuffixRom) != 0)
  {
    strncpy(m_dumpSuffixRom, romName, sizeof(m_dumpSuffixRom) - 1);
    m_dumpSuffixRom[sizeof(m_dumpSuffixRom) - 1] = '\0';
    GenerateRandomSuffix(m_dumpSuffix, 8);
    m_dumpSuffixValid = true;
  }

  strncpy(outSuffix, m_dumpSuffix, outSize - 1);
  outSuffix[outSize - 1] = '\0';
  return true;
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

  (void)m_stopFlag.load(std::memory_order_acquire);

  Config* const pConfig = Config::GetInstance();
  bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();
  bool filterTransitionalFrames = pConfig->IsFilterTransitionalFrames();

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
            if (!GetDumpSuffix(name, suffix, sizeof(suffix)))
            {
              GenerateRandomSuffix(suffix, 8);
            }
            if (m_dumpPath[0] == '\0') strcpy(m_dumpPath, Config::GetInstance()->GetDumpPath());
            size_t pathLen = strlen(m_dumpPath);
            if (pathLen == 0)
            {
              snprintf(filename, sizeof(filename), "./%s-%s.txt", name, suffix);
            }
            else if (m_dumpPath[pathLen - 1] == '/' || m_dumpPath[pathLen - 1] == '\\')
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

void DMD::DumpDMDRgb565Thread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  uint16_t bufferPosition = 0;
  uint16_t renderBuffer[3][256 * 64] = {0};
  uint16_t frameWidths[3] = {0};
  uint16_t frameHeights[3] = {0};
  uint32_t passed[3] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;
  uint8_t palette[256 * 3] = {0};
  uint8_t rgb24Temp[256 * 64 * 3] = {0};

  (void)m_stopFlag.load(std::memory_order_acquire);

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

      Update* update = m_pUpdateBufferQueue[bufferPositionMod];
      if (!(update->hasData || update->hasSegData)) continue;

      if (!(update->mode == Mode::RGB24 || update->mode == Mode::RGB16 || update->mode == Mode::SerumV1 ||
            update->mode == Mode::Vni || IsSerumV2Mode(update->mode)))
        continue;

      bool updateFrame = false;
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
          char filename[DMDUTIL_MAX_NAME_SIZE + 128 + 8 + 9];
          char suffix[9];  // 8 chars + null terminator
          if (!GetDumpSuffix(name, suffix, sizeof(suffix)))
          {
            GenerateRandomSuffix(suffix, 8);
          }
          if (m_dumpPath[0] == '\0') strcpy(m_dumpPath, Config::GetInstance()->GetDumpPath());
          size_t pathLen = strlen(m_dumpPath);
          if (pathLen == 0)
          {
            snprintf(filename, sizeof(filename), "./%s-%s.565.txt", name, suffix);
          }
          else if (m_dumpPath[pathLen - 1] == '/' || m_dumpPath[pathLen - 1] == '\\')
          {
            snprintf(filename, sizeof(filename), "%s%s-%s.565.txt", m_dumpPath, name, suffix);
          }
          else
          {
            snprintf(filename, sizeof(filename), "%s/%s-%s.565.txt", m_dumpPath, name, suffix);
          }
          f = fopen(filename, "w");
          updateFrame = true;
          memset(renderBuffer, 0, sizeof(renderBuffer));
          memset(frameWidths, 0, sizeof(frameWidths));
          memset(frameHeights, 0, sizeof(frameHeights));
          passed[0] = passed[1] = 0;
        }
      }

      if (name[0] == '\0')
      {
        continue;
      }

      uint16_t width = update->width;
      uint16_t height = update->height;
      int length = (int)width * height;
      size_t frameBytes = (size_t)length * sizeof(uint16_t);
      if (frameBytes > sizeof(renderBuffer[0]))
      {
        continue;
      }

      if (width != frameWidths[1] || height != frameHeights[1])
      {
        updateFrame = true;
      }

      uint16_t* nextFrame = renderBuffer[2];
      if (update->mode == Mode::RGB16 || IsSerumV2Mode(update->mode))
      {
        memcpy(nextFrame, update->segData, frameBytes);
      }
      else
      {
        if (update->mode == Mode::RGB24)
        {
          if (update->depth != 24)
          {
            UpdatePalette(palette, update->depth, update->r, update->g, update->b);
          }
          AdjustRGB24Depth(update->data, rgb24Temp, length, palette, update->depth);
        }
        else
        {
          size_t paletteBytes = PaletteBytesForDepth((uint8_t)update->depth);
          if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
          {
            memcpy(palette, update->segData, paletteBytes);
          }
          FrameUtil::Helper::ConvertToRgb24(rgb24Temp, update->data, length, palette);
        }

        for (int i = 0; i < length; i++)
        {
          int pos = i * 3;
          uint32_t r = rgb24Temp[pos];
          uint32_t g = rgb24Temp[pos + 1];
          uint32_t b = rgb24Temp[pos + 2];
          nextFrame[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
        }
      }

      if (updateFrame || memcmp(renderBuffer[1], nextFrame, frameBytes) != 0)
      {
        passed[2] = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count());
        frameWidths[2] = width;
        frameHeights[2] = height;

        if (f && passed[0] > 0 && frameWidths[0] > 0 && frameHeights[0] > 0)
        {
          fprintf(f, "0x%08x\r\n", passed[0]);
          uint32_t rowWidth = frameWidths[0];
          uint32_t rowHeight = frameHeights[0];
          const uint16_t* frame = renderBuffer[0];
          for (uint32_t y = 0; y < rowHeight; y++)
          {
            for (uint32_t x = 0; x < rowWidth; x++)
            {
              fprintf(f, "%04x", frame[y * rowWidth + x]);
            }
            fprintf(f, "\r\n");
          }
          fprintf(f, "\r\n");
        }

        size_t prevBytes = (size_t)frameWidths[1] * frameHeights[1] * sizeof(uint16_t);
        if (prevBytes > sizeof(renderBuffer[0])) prevBytes = sizeof(renderBuffer[0]);
        memcpy(renderBuffer[0], renderBuffer[1], prevBytes);
        passed[0] = passed[1];
        frameWidths[0] = frameWidths[1];
        frameHeights[0] = frameHeights[1];

        memcpy(renderBuffer[1], nextFrame, frameBytes);
        passed[1] = passed[2];
        frameWidths[1] = frameWidths[2];
        frameHeights[1] = frameHeights[2];
      }
    }
  }
}

void DMD::DumpDMDRgb888Thread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  uint16_t bufferPosition = 0;
  uint8_t renderBuffer[3][256 * 64 * 3] = {0};
  uint16_t frameWidths[3] = {0};
  uint16_t frameHeights[3] = {0};
  uint32_t passed[3] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;
  uint8_t palette[256 * 3] = {0};

  (void)m_stopFlag.load(std::memory_order_acquire);

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

      Update* update = m_pUpdateBufferQueue[bufferPositionMod];
      if (!(update->hasData || update->hasSegData)) continue;

      if (!(update->mode == Mode::RGB24 || update->mode == Mode::RGB16 || update->mode == Mode::SerumV1 ||
            update->mode == Mode::Vni || IsSerumV2Mode(update->mode)))
        continue;

      bool updateFrame = false;
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
          char filename[DMDUTIL_MAX_NAME_SIZE + 128 + 8 + 9];
          char suffix[9];  // 8 chars + null terminator
          if (!GetDumpSuffix(name, suffix, sizeof(suffix)))
          {
            GenerateRandomSuffix(suffix, 8);
          }
          if (m_dumpPath[0] == '\0') strcpy(m_dumpPath, Config::GetInstance()->GetDumpPath());
          size_t pathLen = strlen(m_dumpPath);
          if (pathLen == 0)
          {
            snprintf(filename, sizeof(filename), "./%s-%s.888.txt", name, suffix);
          }
          else if (m_dumpPath[pathLen - 1] == '/' || m_dumpPath[pathLen - 1] == '\\')
          {
            snprintf(filename, sizeof(filename), "%s%s-%s.888.txt", m_dumpPath, name, suffix);
          }
          else
          {
            snprintf(filename, sizeof(filename), "%s/%s-%s.888.txt", m_dumpPath, name, suffix);
          }
          f = fopen(filename, "w");
          updateFrame = true;
          memset(renderBuffer, 0, sizeof(renderBuffer));
          memset(frameWidths, 0, sizeof(frameWidths));
          memset(frameHeights, 0, sizeof(frameHeights));
          passed[0] = passed[1] = 0;
        }
      }

      if (name[0] == '\0')
      {
        continue;
      }

      uint16_t width = update->width;
      uint16_t height = update->height;
      int length = (int)width * height;
      size_t frameBytes = (size_t)length * 3;
      if (frameBytes > sizeof(renderBuffer[0]))
      {
        continue;
      }

      if (width != frameWidths[1] || height != frameHeights[1])
      {
        updateFrame = true;
      }

      uint8_t* nextFrame = renderBuffer[2];
      if (update->mode == Mode::RGB24)
      {
        if (update->depth != 24)
        {
          UpdatePalette(palette, update->depth, update->r, update->g, update->b);
        }
        AdjustRGB24Depth(update->data, nextFrame, length, palette, update->depth);
      }
      else if (update->mode == Mode::RGB16 || IsSerumV2Mode(update->mode))
      {
        const uint16_t* src = update->segData;
        for (int i = 0; i < length; i++)
        {
          uint16_t value = src[i];
          uint8_t r = (uint8_t)((value >> 11) & 0x1F);
          uint8_t g = (uint8_t)((value >> 5) & 0x3F);
          uint8_t b = (uint8_t)(value & 0x1F);
          nextFrame[i * 3] = (uint8_t)((r << 3) | (r >> 2));
          nextFrame[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
          nextFrame[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
        }
      }
      else
      {
        size_t paletteBytes = PaletteBytesForDepth((uint8_t)update->depth);
        if (paletteBytes > 0 && paletteBytes <= sizeof(palette))
        {
          memcpy(palette, update->segData, paletteBytes);
        }
        FrameUtil::Helper::ConvertToRgb24(nextFrame, update->data, length, palette);
      }

      if (updateFrame || memcmp(renderBuffer[1], nextFrame, frameBytes) != 0)
      {
        passed[2] = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count());
        frameWidths[2] = width;
        frameHeights[2] = height;

        if (f && passed[0] > 0 && frameWidths[0] > 0 && frameHeights[0] > 0)
        {
          fprintf(f, "0x%08x\r\n", passed[0]);
          uint32_t rowWidth = frameWidths[0];
          uint32_t rowHeight = frameHeights[0];
          const uint8_t* frame = renderBuffer[0];
          for (uint32_t y = 0; y < rowHeight; y++)
          {
            for (uint32_t x = 0; x < rowWidth; x++)
            {
              int pos = (int)(y * rowWidth + x) * 3;
              fprintf(f, "%02x%02x%02x", frame[pos], frame[pos + 1], frame[pos + 2]);
            }
            fprintf(f, "\r\n");
          }
          fprintf(f, "\r\n");
        }

        size_t prevBytes = (size_t)frameWidths[1] * frameHeights[1] * 3;
        if (prevBytes > sizeof(renderBuffer[0])) prevBytes = sizeof(renderBuffer[0]);
        memcpy(renderBuffer[0], renderBuffer[1], prevBytes);
        passed[0] = passed[1];
        frameWidths[0] = frameWidths[1];
        frameHeights[0] = frameHeights[1];

        memcpy(renderBuffer[1], nextFrame, frameBytes);
        passed[1] = passed[2];
        frameWidths[1] = frameWidths[2];
        frameHeights[1] = frameHeights[2];
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

  (void)m_stopFlag.load(std::memory_order_acquire);

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

  (void)m_stopFlag.load(std::memory_order_acquire);

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
    //    uint16_t id = m_pGenerator->getSceneId(source, event, value);
    //    if (id > 0)
    //    {
    //      while (m_pupSceneId.load(std::memory_order_acquire) != 0)
    //      {
    //        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //      }
    //      m_pupSceneId.store(id, std::memory_order_release);
    //    }
  }
}

}  // namespace DMDUtil
