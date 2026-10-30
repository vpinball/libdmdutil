#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "DMDUtil/DMD.h"

#include "DMDUtil/Config.h"
#include "DMDUtil/ConsoleDMD.h"
#include "DMDUtil/LevelDMD.h"
#include "DMDUtil/RGB24DMD.h"

#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#include <winsock2.h>  // Windows byte-order functions
#else
#include <arpa/inet.h>  // Linux/macOS byte-order functions
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_set>

#include "AlphaNumeric.h"
#include "DMDInternal.h"
#include "DMDUtil/Logger.h"
#include "FrameUtil.h"
#include "OutputFilters.h"
#include "ZeDMD.h"
#ifdef DMDUTIL_ENABLE_PUP
#include "pupdmd.h"
#endif
#include "komihash/komihash.h"
#include "miniz/miniz.h"
#include "sockpp/tcp_connector.h"

namespace
{
uint64_t SplitMix64(uint64_t value)
{
  value += 0x9E3779B97F4A7C15ull;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

bool ZipDumpFile(const std::string& sourcePath)
{
  if (sourcePath.empty())
  {
    return false;
  }

  std::filesystem::path srcPath(sourcePath);
  std::string zipPath = sourcePath + ".zip";
  std::string entryName = srcPath.filename().string();

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_writer_init_file(&zip, zipPath.c_str(), 0))
  {
    return false;
  }

  bool ok = mz_zip_writer_add_file(&zip, entryName.c_str(), sourcePath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION);
  ok = ok && mz_zip_writer_finalize_archive(&zip);
  mz_zip_writer_end(&zip);

  if (!ok)
  {
    std::error_code ec;
    std::filesystem::remove(zipPath, ec);
    return false;
  }

  std::error_code ec;
  std::filesystem::remove(sourcePath, ec);
  return true;
}

void CloseDumpFile(FILE*& handle, std::string& path, bool zip)
{
  if (!handle) return;
  fflush(handle);
  fclose(handle);
  handle = nullptr;
  if (zip && !path.empty()) ZipDumpFile(path);
  path.clear();
}

}  // namespace

namespace DMDUtil
{

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

std::atomic<bool> DMD::m_finding{false};

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
    m_updateBufferQueueTimestamp[i] = 0;
    m_updateBufferQueueHasTimestamp[i] = false;
    m_updateBufferQueueFrameContext[i] = FrameContext{};
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
  m_pPupDMDThread = nullptr;
  m_pSerumThread = nullptr;
  m_pVniThread = nullptr;
  m_pPixelcadeDMD = nullptr;
  m_pPixelcadeDMDThread = nullptr;
  m_pPinDMD3 = nullptr;
  m_pPinDMD3Thread = nullptr;
  m_pDMDExtStream = nullptr;
  m_pDMDExtStreamThread = nullptr;
  m_pPIN2DMDThread = nullptr;
  m_PIN2DMDConnected = false;
  m_PIN2DMDWidth = 0;
  m_PIN2DMDHeight = 0;

  m_pDMDServerSenderThread = nullptr;
  m_pDmdFrameThread = new std::thread(&DMD::DmdFrameThread, this);
#ifdef DMDUTIL_ENABLE_PUP
  m_pPupDMDThread = new std::thread(&DMD::PupDMDThread, this);
#endif
#ifdef DMDUTIL_ENABLE_SERUM
  m_pSerumThread = new std::thread(&DMD::SerumThread, this);
#endif
#ifdef DMDUTIL_ENABLE_VNI
  m_pVniThread = new std::thread(&DMD::VniThread, this);
#endif
  m_pDMDServerConnector = nullptr;
}

DMD::~DMD()
{
  Log(DMDUtil_LogLevel_INFO, "DMD destructor start");
  if (m_findDisplaysThread.joinable()) m_findDisplaysThread.join();
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  m_stopFlag.store(true, std::memory_order_release);
  ul.unlock();
  m_dmdCV.notify_all();
  {
    std::lock_guard<std::mutex> lock(m_dmdServerQueueMutex);
  }
  m_dmdServerQueueCv.notify_all();

  Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining DmdFrameThread");
  if (m_pDmdFrameThread->joinable())
    m_pDmdFrameThread->join();
  else
    Log(DMDUtil_LogLevel_ERROR, "DMD destructor: DmdFrameThread not joinable");
  delete m_pDmdFrameThread;
  m_pDmdFrameThread = nullptr;

  if (m_pLevelDMDThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining LevelDMDThread");
    if (m_pLevelDMDThread->joinable())
      m_pLevelDMDThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: LevelDMDThread not joinable");
    delete m_pLevelDMDThread;
    m_pLevelDMDThread = nullptr;
  }

  if (m_pRGB24DMDThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining RGB24DMDThread");
    if (m_pRGB24DMDThread->joinable())
      m_pRGB24DMDThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: RGB24DMDThread not joinable");
    delete m_pRGB24DMDThread;
    m_pRGB24DMDThread = nullptr;
  }

  if (m_pConsoleDMDThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining ConsoleDMDThread");
    if (m_pConsoleDMDThread->joinable())
      m_pConsoleDMDThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: ConsoleDMDThread not joinable");
    delete m_pConsoleDMDThread;
    m_pConsoleDMDThread = nullptr;
  }

  if (m_pZeDMDThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining ZeDMDThread");
    if (m_pZeDMDThread->joinable())
      m_pZeDMDThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: ZeDMDThread not joinable");
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

#ifdef DMDUTIL_ENABLE_PUP
  if (m_pPupDMDThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining PupDMDThread");
    if (m_pPupDMDThread->joinable())
      m_pPupDMDThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: PupDMDThread not joinable");
    delete m_pPupDMDThread;
    m_pPupDMDThread = nullptr;
  }
#endif

#ifdef DMDUTIL_ENABLE_SERUM
  if (m_pSerumThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining SerumThread");
    if (m_pSerumThread->joinable())
      m_pSerumThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: SerumThread not joinable");
    delete m_pSerumThread;
    m_pSerumThread = nullptr;
  }
#endif

#ifdef DMDUTIL_ENABLE_VNI
  if (m_pVniThread)
  {
    Log(DMDUtil_LogLevel_INFO, "DMD destructor: joining VniThread");
    if (m_pVniThread->joinable())
      m_pVniThread->join();
    else
      Log(DMDUtil_LogLevel_ERROR, "DMD destructor: VniThread not joinable");
    delete m_pVniThread;
    m_pVniThread = nullptr;
  }
#endif

  for (std::thread** ppThread : {&m_pPixelcadeDMDThread, &m_pPinDMD3Thread, &m_pDMDExtStreamThread, &m_pPIN2DMDThread})
  {
    if (*ppThread)
    {
      (*ppThread)->join();
      delete *ppThread;
      *ppThread = nullptr;
    }
  }

  delete m_pAlphaNumeric;
  delete m_pZeDMD;
#ifdef DMDUTIL_ENABLE_PUP
  delete m_pPUPDMD;
#endif
#ifdef DMDUTIL_WITH_PIXELCADE
  delete m_pPixelcadeDMD;
#endif
#ifdef DMDUTIL_WITH_PINDMD3
  delete m_pPinDMD3;
#endif
#ifdef DMDUTIL_WITH_DMDEXTSTREAM
  delete m_pDMDExtStream;
#endif

  for (LevelDMD* pLevelDMD : m_levelDMDs) delete pLevelDMD;
  for (RGB24DMD* pRGB24DMD : m_rgb24DMDs) delete pRGB24DMD;
  for (ConsoleDMD* pConsoleDMD : m_consoleDMDs) delete pConsoleDMD;

  if (m_pDMDServerSenderThread)
  {
    m_pDMDServerSenderThread->join();
    delete m_pDMDServerSenderThread;
    m_pDMDServerSenderThread = nullptr;
  }

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

  Log(DMDUtil_LogLevel_INFO, "DMD destructor finished");
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
    if (m_pDMDServerConnector)
    {
      m_pDMDServerSenderThread = new std::thread(&DMD::DMDServerSenderThread, this);
    }
    else
    {
      Log(DMDUtil_LogLevel_INFO, "DMDServer connection to %s:%d failed!", pConfig->GetDMDServerAddr(),
          pConfig->GetDMDServerPort());
    }
  }
  return (m_pDMDServerConnector);
}

bool DMD::IsFinding() { return m_finding.load(std::memory_order_acquire); }

bool DMD::HasDisplay() const
{
  if (m_pZeDMD != nullptr || m_rgb24DMDs.size() > 0)
  {
    return true;
  }

  if (m_pPixelcadeDMD != nullptr) return true;
  if (m_pPinDMD3 != nullptr) return true;
  if (m_pDMDExtStream != nullptr) return true;
  if (m_PIN2DMDConnected) return true;

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

  if (m_PIN2DMDConnected && m_PIN2DMDWidth == 256) return true;

  return false;
}

void DMD::SetRomName(const char* name) { snprintf(m_romName, sizeof(m_romName), "%s", name ? name : ""); }

void DMD::SetAltColorPath(const char* path)
{
  snprintf(m_altColorPath, sizeof(m_altColorPath), "%s", path ? path : "");
}

void DMD::SetPUPVideosPath(const char* path)
{
  snprintf(m_pupVideosPath, sizeof(m_pupVideosPath), "%s", path ? path : "");
}

void DMD::DumpDMDTxt()
{
  if (!m_pDumpDMDTxtThread)
  {
    m_pDumpDMDTxtThread = new std::thread(&DMD::DumpDMDTxtThread, this);
  }
}

void DMD::DumpDMDRaw()
{
  if (!m_pDumpDMDRawThread)
  {
    m_pDumpDMDRawThread = new std::thread(&DMD::DumpDMDRawThread, this);
  }
}

void DMD::DumpDMDRgb565()
{
  if (!m_pDumpDMDRgb565Thread)
  {
    m_pDumpDMDRgb565Thread = new std::thread(&DMD::DumpDMDColorThread, this, false);
  }
}

void DMD::DumpDMDRgb888()
{
  if (!m_pDumpDMDRgb888Thread)
  {
    m_pDumpDMDRgb888Thread = new std::thread(&DMD::DumpDMDColorThread, this, true);
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

    return true;
  }
  return false;
}

void DMD::QueueFrame(Mode mode, const void* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                     uint8_t b, bool buffered, bool hasTimestamp, uint32_t timestampMs,
                     const FrameContext* pFrameContext)
{
  auto dmdUpdate = std::make_shared<Update>();
  dmdUpdate->mode = mode;
  dmdUpdate->depth = depth;
  dmdUpdate->width = width;
  dmdUpdate->height = height;
  dmdUpdate->r = r;
  dmdUpdate->g = g;
  dmdUpdate->b = b;
  dmdUpdate->hasSegData = false;
  dmdUpdate->hasSegData2 = false;
  dmdUpdate->hasData = pData != nullptr;

  if (pData)
  {
    const size_t pixels = (size_t)width * height;
    if (mode == Mode::RGB16)
      memcpy(dmdUpdate->segData, pData, pixels * sizeof(uint16_t));
    else
      memcpy(dmdUpdate->data, pData, pixels * (mode == Mode::RGB24 ? 3 : 1));
  }

  QueueUpdate(dmdUpdate, buffered, hasTimestamp, timestampMs, pFrameContext);
}

void DMD::QueueUpdate(const std::shared_ptr<Update> dmdUpdate, bool buffered, bool hasTimestamp, uint32_t timestampMs,
                      const FrameContext* frameContext)
{
  std::unique_lock<std::shared_mutex> ul(m_dmdSharedMutex);
  uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
  uint8_t slot = (++updateBufferQueuePosition) % DMDUTIL_FRAME_BUFFER_SIZE;
  memcpy(m_pUpdateBufferQueue[slot], dmdUpdate.get(), sizeof(Update));
  m_updateBufferQueueHasTimestamp[slot] = hasTimestamp;
  m_updateBufferQueueTimestamp[slot] = timestampMs;
  m_updateBufferQueueFrameContext[slot] = frameContext ? *frameContext : FrameContext{};
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

  const bool sendToDMDServer = !IsSerumMode(dmdUpdate->mode) || dmdUpdate->mode == Mode::SerumCommand;
  if (m_pDMDServerConnector && sendToDMDServer && !m_dmdServerConnectionLost.load(std::memory_order_acquire))
  {
    std::lock_guard<std::mutex> lock(m_dmdServerQueueMutex);
    m_dmdServerQueue.push_back({dmdUpdate, buffered});
    m_dmdServerQueueCv.notify_one();
  }
}

void DMD::DMDServerSenderThread()
{
  while (true)
  {
    std::unique_lock<std::mutex> lock(m_dmdServerQueueMutex);
    m_dmdServerQueueCv.wait(lock,
                            [&]() { return m_stopFlag.load(std::memory_order_relaxed) || !m_dmdServerQueue.empty(); });
    if (m_stopFlag.load(std::memory_order_acquire)) return;

    DMDServerQueueEntry entry = std::move(m_dmdServerQueue.front());
    m_dmdServerQueue.pop_front();
    lock.unlock();

    StreamHeader streamHeader;
    streamHeader.buffered = (uint8_t)entry.buffered;
    streamHeader.disconnectOthers = (uint8_t)m_dmdServerDisconnectOthers;
    streamHeader.convertToNetworkByteOrder();
    PathsHeader pathsHeader;
    snprintf(pathsHeader.name, sizeof(pathsHeader.name), "%s", m_romName);
    snprintf(pathsHeader.altColorPath, sizeof(pathsHeader.altColorPath), "%s", m_altColorPath);
    snprintf(pathsHeader.pupVideosPath, sizeof(pathsHeader.pupVideosPath), "%s", m_pupVideosPath);
    pathsHeader.convertToNetworkByteOrder();
    Update dmdUpdateNetwork = entry.update->toNetworkByteOrder();

    const bool written = m_pDMDServerConnector->Write(&streamHeader, sizeof(StreamHeader)) == sizeof(StreamHeader) &&
                         m_pDMDServerConnector->Write(&pathsHeader, sizeof(PathsHeader)) == sizeof(PathsHeader) &&
                         m_pDMDServerConnector->Write(&dmdUpdateNetwork, sizeof(Update)) == sizeof(Update);
    if (!written)
    {
      Log(DMDUtil_LogLevel_ERROR, "DMDServer connection lost, no more frames will be sent");
      m_dmdServerConnectionLost.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> queueLock(m_dmdServerQueueMutex);
      m_dmdServerQueue.clear();
      return;
    }

    if (streamHeader.disconnectOthers != 0) m_dmdServerDisconnectOthers = false;
  }
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
  QueueFrame(Mode::Data, pData, depth, width, height, r, g, b, buffered, false, 0, nullptr);
}

void DMD::UpdateDataWithTimestamp(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r,
                                  uint8_t g, uint8_t b, uint32_t timestampMs, bool buffered)
{
  QueueFrame(Mode::Data, pData, depth, width, height, r, g, b, buffered, true, timestampMs, nullptr);
}

void DMD::UpdateDataWithMetadataAndTimestamp(const uint8_t* pData, int depth, uint16_t width, uint16_t height,
                                             uint8_t r, uint8_t g, uint8_t b, uint32_t timestampMs,
                                             const FrameContext& frameContext, bool buffered)
{
  QueueFrame(Mode::Data, pData, depth, width, height, r, g, b, buffered, true, timestampMs, &frameContext);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                          uint8_t b, bool buffered)
{
  QueueFrame(Mode::RGB24, pData, depth, width, height, r, g, b, buffered, false, 0, nullptr);
}

void DMD::UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  QueueFrame(Mode::RGB24, pData, 24, width, height, 0, 0, 0, buffered, false, 0, nullptr);
}

void DMD::UpdateRGB24DataWithTimestamp(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r,
                                       uint8_t g, uint8_t b, uint32_t timestampMs, bool buffered)
{
  QueueFrame(Mode::RGB24, pData, depth, width, height, r, g, b, buffered, true, timestampMs, nullptr);
}

void DMD::UpdateRGB24DataWithTimestamp(const uint8_t* pData, uint16_t width, uint16_t height, uint32_t timestampMs,
                                       bool buffered)
{
  QueueFrame(Mode::RGB24, pData, 24, width, height, 0, 0, 0, buffered, true, timestampMs, nullptr);
}

void DMD::UpdateRGB24DataWithMetadataAndTimestamp(const uint8_t* pData, uint16_t width, uint16_t height,
                                                  uint32_t timestampMs, const FrameContext& frameContext, bool buffered)
{
  QueueFrame(Mode::RGB24, pData, 24, width, height, 0, 0, 0, buffered, true, timestampMs, &frameContext);
}

void DMD::UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  QueueFrame(Mode::RGB16, pData, 24, width, height, 255, 255, 255, buffered, false, 0, nullptr);
}

void DMD::UpdateRGB16DataWithTimestamp(const uint16_t* pData, uint16_t width, uint16_t height, uint32_t timestampMs,
                                       bool buffered)
{
  QueueFrame(Mode::RGB16, pData, 24, width, height, 255, 255, 255, buffered, true, timestampMs, nullptr);
}

void DMD::UpdateRGB16DataWithMetadataAndTimestamp(const uint16_t* pData, uint16_t width, uint16_t height,
                                                  uint32_t timestampMs, const FrameContext& frameContext, bool buffered)
{
  QueueFrame(Mode::RGB16, pData, 24, width, height, 255, 255, 255, buffered, true, timestampMs, &frameContext);
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

bool DMD::WaitForSerumColorizeCapture(uint64_t sourceOrdinal, SerumCapture& capture, uint32_t timeoutMs)
{
  std::unique_lock<std::mutex> lock(m_serumCaptureMutex);
  auto ready = [&]() { return m_serumColorizeCaptures.find(sourceOrdinal) != m_serumColorizeCaptures.end(); };
  if (timeoutMs == 0)
  {
    if (!ready())
    {
      return false;
    }
  }
  else if (!m_serumCaptureCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready))
  {
    return false;
  }

  auto captureIt = m_serumColorizeCaptures.find(sourceOrdinal);
  if (captureIt == m_serumColorizeCaptures.end())
  {
    return false;
  }

  capture = captureIt->second;
  m_serumColorizeCaptures.erase(m_serumColorizeCaptures.begin(), std::next(captureIt));
  return true;
}

void DMD::FindDisplays()
{
  if (m_finding.load(std::memory_order_acquire)) return;

  Config* const pConfig = Config::GetInstance();

  if (pConfig->IsDmdServer())
  {
    ConnectDMDServer();
  }

  if (pConfig->IsLocalDisplaysActive())
  {
    m_finding.store(true, std::memory_order_release);

    if (m_findDisplaysThread.joinable()) m_findDisplaysThread.join();
    m_findDisplaysThread = std::thread(
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

#ifdef DMDUTIL_WITH_PIXELCADE
          m_pPixelcadeDMD = nullptr;

          if (pConfig->IsPixelcade())
          {
            m_pPixelcadeDMD = PixelcadeDMD::Connect(pConfig->GetPixelcadeDevice());
            if (m_pPixelcadeDMD) m_pPixelcadeDMDThread = new std::thread(&DMD::PixelcadeDMDThread, this);
          }
#endif

#ifdef DMDUTIL_WITH_PINDMD3
          m_pPinDMD3 = nullptr;

          if (pConfig->IsPinDMD3())
          {
            m_pPinDMD3 = PinDMD3::Connect(pConfig->GetPinDMD3Device());
            if (m_pPinDMD3) m_pPinDMD3Thread = new std::thread(&DMD::PinDMD3Thread, this);
          }
#endif

#ifdef DMDUTIL_WITH_DMDEXTSTREAM
          m_pDMDExtStream = nullptr;

          if (pConfig->IsDMDExtStream())
          {
            m_pDMDExtStream = DMDExtStream::Create(pConfig->GetDMDExtStreamUrl(), pConfig->IsDMDExtStreamRetry(),
                                                   pConfig->GetDMDExtStreamRetryInterval());
            if (m_pDMDExtStream) m_pDMDExtStreamThread = new std::thread(&DMD::DMDExtStreamThread, this);
          }
#endif

#ifdef DMDUTIL_WITH_PIN2DMD
          m_PIN2DMDConnected = false;
          m_PIN2DMDWidth = 0;
          m_PIN2DMDHeight = 0;

          if (pConfig->IsPIN2DMD())
          {
            int PIN2DMDInitResult = PIN2DMDInit();
            if (PIN2DMDInitResult > 0)
            {
              m_PIN2DMDWidth = PIN2DMDGetWidth();
              m_PIN2DMDHeight = PIN2DMDGetHeight();
              if (m_PIN2DMDWidth > 0 && m_PIN2DMDHeight > 0)
              {
                m_PIN2DMDConnected = true;
                m_pPIN2DMDThread = new std::thread(&DMD::PIN2DMDThread, this);
              }
            }
          }
#endif

          m_finding.store(false, std::memory_order_release);
        });
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

void DMD::OutputLoop(bool everyFrame, uint32_t idleTimeoutMs, const std::function<void()>& onIdle,
                     const OutputHandler& onUpdate)
{
  uint16_t bufferPosition = 0;

  while (true)
  {
    std::shared_lock<std::shared_mutex> sl(m_dmdSharedMutex);
    auto hasWork = [&]()
    {
      return m_stopFlag.load(std::memory_order_relaxed) ||
             (m_updateBufferQueuePosition.load(std::memory_order_relaxed) != bufferPosition);
    };
    if (idleTimeoutMs > 0)
      m_dmdCV.wait_for(sl, std::chrono::milliseconds(idleTimeoutMs), hasWork);
    else
      m_dmdCV.wait(sl, hasWork);
    sl.unlock();

    if (m_stopFlag.load(std::memory_order_acquire)) return;

    if (onIdle) onIdle();

    const uint16_t updateBufferQueuePosition = m_updateBufferQueuePosition.load(std::memory_order_acquire);
    while (!m_stopFlag.load(std::memory_order_relaxed) && bufferPosition != updateBufferQueuePosition)
    {
      const uint16_t nextPosition = everyFrame ? (uint16_t)(bufferPosition + 1)
                                               : GetNextBufferQueuePosition(bufferPosition, updateBufferQueuePosition);
      const uint16_t skipped = (uint16_t)(nextPosition - bufferPosition - 1);
      bufferPosition = nextPosition;
      onUpdate((uint8_t)(bufferPosition % DMDUTIL_FRAME_BUFFER_SIZE), bufferPosition, skipped);
    }
  }
}

bool DMD::PrefersSerumFrame(uint16_t targetWidth, Mode mode) const
{
  if (targetWidth == 0) return true;
  if (targetWidth >= 256 && mode == Mode::SerumV2_32_64) return false;
  if (targetWidth < 256 && mode == Mode::SerumV2_64_32) return false;
  return true;
}

bool DMD::ShouldRenderUpdate(Mode mode, bool excludeColorized, bool showNotColorized, uint16_t targetWidth)
{
  if (excludeColorized) return !IsSerumMode(mode, true);

  if (m_pSerum || m_pVni)
  {
    if (!IsSerumMode(mode, showNotColorized)) return false;
    if (!PrefersSerumFrame(targetWidth, mode)) return false;
  }

  return true;
}

bool DMD::ConvertColorUpdateToRgb24(const Update& update, uint8_t* pPalette, uint8_t* pRgb24)
{
  const int length = (int)update.width * update.height;

  if (update.mode == Mode::RGB24)
  {
    if (update.depth != 24) UpdatePalette(pPalette, (uint8_t)update.depth, update.r, update.g, update.b);
    AdjustRGB24Depth(update.data, pRgb24, length, pPalette, (uint8_t)update.depth);
    return true;
  }

  if (update.mode == Mode::RGB16 || IsSerumV2Mode(update.mode))
  {
    Rgb565ToRgb888(update.segData, pRgb24, (size_t)length);
    return true;
  }

  if (update.mode == Mode::SerumV1 || update.mode == Mode::Vni)
  {
    const size_t paletteBytes = PaletteBytesForDepth((uint8_t)update.depth);
    if (paletteBytes > 0 && paletteBytes <= 256 * 3) memcpy(pPalette, update.segData, paletteBytes);
    FrameUtil::Helper::ConvertToRgb24(pRgb24, const_cast<uint8_t*>(update.data), length, pPalette);
    return true;
  }

  return false;
}

DMD::DecodedKind DMD::DecodeUpdate(const Update& update, OutputDecoder& d)
{
  if (!ShouldRenderUpdate(update.mode, d.excludeColorized, d.showNotColorized, d.targetWidth)) return DecodedKind::None;
  if (!(update.hasData || update.hasSegData)) return DecodedKind::None;

  const size_t length = (size_t)update.width * update.height;
  if (length == 0 || length > DMDUTIL_MAX_FRAME_PIXELS) return DecodedKind::None;

  const bool contextChanged =
      update.width != d.width || update.height != d.height || update.depth != d.depth || update.mode != d.mode;
  bool paletteChanged = false;
  if (update.depth != 24)
    paletteChanged = UpdatePalette(d.palette, (uint8_t)update.depth, update.r, update.g, update.b);

  auto commit = [&](DecodedKind kind)
  {
    d.mode = update.mode;
    d.width = update.width;
    d.height = update.height;
    d.depth = (uint8_t)update.depth;
    d.r = update.r;
    d.g = update.g;
    d.b = update.b;
    return kind;
  };

  d.rgb565Source = false;

  switch (update.mode)
  {
    case Mode::RGB24:
      if (!update.hasData) return DecodedKind::None;
      if (!contextChanged && !paletteChanged && memcmp(d.rawRgb24, update.data, length * 3) == 0)
        return DecodedKind::None;
      memcpy(d.rawRgb24, update.data, length * 3);
      AdjustRGB24Depth(update.data, d.rgb24, (int)length, d.palette, (uint8_t)update.depth);
      return commit(DecodedKind::Rgb24);

    case Mode::RGB16:
    case Mode::SerumV2_32:
    case Mode::SerumV2_32_64:
    case Mode::SerumV2_64:
    case Mode::SerumV2_64_32:
      ConvertColorUpdateToRgb24(update, d.palette, d.rgb24);
      d.rgb565Source = true;
      return commit(DecodedKind::Rgb24);

    case Mode::SerumV1:
    case Mode::Vni:
    {
      if (!update.hasData) return DecodedKind::None;
      const size_t paletteBytes = PaletteBytesForDepth((uint8_t)update.depth);
      if (paletteBytes > 0 && paletteBytes <= sizeof(d.palette)) memcpy(d.palette, update.segData, paletteBytes);
      memcpy(d.indexed, update.data, length);
      FrameUtil::Helper::ConvertToRgb24(d.rgb24, d.indexed, (int)length, d.palette);
      return commit(DecodedKind::Indexed);
    }

    case Mode::Data:
    case Mode::NotColorized:
      if (update.mode == Mode::NotColorized && !d.showNotColorized) return DecodedKind::None;
      if (!update.hasData) return DecodedKind::None;
      if (!contextChanged && !paletteChanged && memcmp(d.indexed, update.data, length) == 0) return DecodedKind::None;
      memcpy(d.indexed, update.data, length);
      FrameUtil::Helper::ConvertToRgb24(d.rgb24, d.indexed, (int)length, d.palette);
      return commit(DecodedKind::Indexed);

    case Mode::AlphaNumeric:
    {
      bool changed = contextChanged || paletteChanged;
      if (memcmp(d.segData1, update.segData, sizeof(d.segData1)) != 0)
      {
        memcpy(d.segData1, update.segData, sizeof(d.segData1));
        changed = true;
      }
      if (update.hasSegData2 && memcmp(d.segData2, update.segData2, sizeof(d.segData2)) != 0)
      {
        memcpy(d.segData2, update.segData2, sizeof(d.segData2));
        changed = true;
      }
      if (!changed) return DecodedKind::None;
      if (update.hasSegData2)
        m_pAlphaNumeric->Render(d.indexed, update.layout, d.segData1, d.segData2);
      else
        m_pAlphaNumeric->Render(d.indexed, update.layout, d.segData1);
      FrameUtil::Helper::ConvertToRgb24(d.rgb24, d.indexed, (int)length, d.palette);
      return commit(DecodedKind::Indexed);
    }

    default:
      return DecodedKind::None;
  }
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

void DMD::LevelDMDThread()
{
  std::vector<uint8_t> renderBuffer(DMDUTIL_MAX_FRAME_PIXELS, 0);

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               const Update& update = *m_pUpdateBufferQueue[slot];
               if (m_levelDMDs.empty() || update.mode != Mode::Data || !update.hasData) return;

               const size_t length = (size_t)update.width * update.height;
               if (length == 0 || length > DMDUTIL_MAX_FRAME_PIXELS) return;
               if (memcmp(renderBuffer.data(), update.data, length) == 0) return;

               memcpy(renderBuffer.data(), update.data, length);
               for (LevelDMD* pLevelDMD : m_levelDMDs)
               {
                 if (pLevelDMD->GetLength() == (int)length)
                   pLevelDMD->Update(renderBuffer.data(), (uint8_t)update.depth);
               }
             });
}

void DMD::RGB24DMDThread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForRGB24DMD();

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               if (m_rgb24DMDs.empty()) return;
               if (DecodeUpdate(*m_pUpdateBufferQueue[slot], *decoder) == DecodedKind::None) return;

               for (RGB24DMD* pRGB24DMD : m_rgb24DMDs)
               {
                 if (!PrefersSerumFrame((uint16_t)pRGB24DMD->GetWidth(), decoder->mode)) continue;
                 pRGB24DMD->Update(decoder->rgb24, decoder->width, decoder->height);
               }
             });
}

void DMD::ConsoleDMDThread()
{
  std::vector<uint8_t> renderBuffer(DMDUTIL_MAX_FRAME_PIXELS, 0);

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               const Update& update = *m_pUpdateBufferQueue[slot];
               if (m_consoleDMDs.empty() || update.mode != Mode::Data || !update.hasData) return;

               const size_t length = (size_t)update.width * update.height;
               if (length == 0 || length > DMDUTIL_MAX_FRAME_PIXELS) return;
               if (memcmp(renderBuffer.data(), update.data, length) == 0) return;

               memcpy(renderBuffer.data(), update.data, length);
               for (ConsoleDMD* pConsoleDMD : m_consoleDMDs)
               {
                 pConsoleDMD->Render(renderBuffer.data(), update.width, update.height, (uint8_t)update.depth);
               }
             });
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

void DMD::AdjustRGB24Depth(const uint8_t* pData, uint8_t* pDstData, int length, const uint8_t* palette, uint8_t depth)
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
  if (!buffer || length == 0)
  {
    return;
  }

  static std::atomic<uint64_t> sequence{0};
  const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  const uint64_t charsetSize = (uint64_t)(sizeof(charset) - 1);  // exclude null terminator

  const uint64_t nowSteady = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
  const uint64_t nowSystem = (uint64_t)std::chrono::system_clock::now().time_since_epoch().count();
  const uint64_t threadHash = (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
#if defined(_WIN32) || defined(_WIN64)
  const uint64_t processId = (uint64_t)_getpid();
#else
  const uint64_t processId = (uint64_t)getpid();
#endif
  uint64_t state = SplitMix64(nowSteady ^ nowSystem ^ threadHash ^ processId ^ sequence.fetch_add(1));

  for (size_t i = 0; i < length; ++i)
  {
    state = SplitMix64(state);
    buffer[i] = charset[state % charsetSize];
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

bool DMD::GetQueueTimestamp(uint8_t bufferPositionMod, uint32_t& timestampMs) const
{
  if (!m_updateBufferQueueHasTimestamp[bufferPositionMod]) return false;
  timestampMs = m_updateBufferQueueTimestamp[bufferPositionMod];
  return true;
}

uint16_t DMD::GetUpdateQueuePosition() const { return m_updateBufferQueuePosition.load(std::memory_order_acquire); }

bool DMD::DumpersReached(uint16_t targetPosition) const
{
  if (m_dumpTxtActive.load(std::memory_order_acquire) &&
      m_dumpTxtPosition.load(std::memory_order_acquire) != targetPosition)
    return false;
  if (m_dumpRawActive.load(std::memory_order_acquire) &&
      m_dumpRawPosition.load(std::memory_order_acquire) != targetPosition)
    return false;
  if (m_dump565Active.load(std::memory_order_acquire) &&
      m_dump565Position.load(std::memory_order_acquire) != targetPosition)
    return false;
  if (m_dump888Active.load(std::memory_order_acquire) &&
      m_dump888Position.load(std::memory_order_acquire) != targetPosition)
    return false;
  return true;
}

bool DMD::GetQueueFrameContext(uint8_t bufferPositionMod, FrameContext& frameContext) const
{
  if (bufferPositionMod >= DMDUTIL_FRAME_BUFFER_SIZE)
  {
    return false;
  }
  frameContext = m_updateBufferQueueFrameContext[bufferPositionMod];
  return frameContext.valid;
}

bool DMD::WaitForDumpers(uint16_t targetPosition, uint32_t timeoutMs)
{
  std::unique_lock<std::mutex> lock(m_dumpPositionMutex);
  if (DumpersReached(targetPosition)) return true;
  if (timeoutMs == 0) return false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  return m_dumpPositionCv.wait_until(lock, deadline, [&]() { return DumpersReached(targetPosition); });
}

FILE* DMD::OpenDumpFile(const char* name, const char* extension, std::string& outPath)
{
  char suffix[9];
  if (!GetDumpSuffix(name, suffix, sizeof(suffix))) GenerateRandomSuffix(suffix, 8);
  if (m_dumpPath[0] == '\0') snprintf(m_dumpPath, sizeof(m_dumpPath), "%s", Config::GetInstance()->GetDumpPath());

  std::string path = m_dumpPath;
  if (path.empty())
    path = "./";
  else if (path.back() != '/' && path.back() != '\\')
    path += '/';
  path += name;
  path += '-';
  path += suffix;
  path += extension;

  FILE* f = fopen(path.c_str(), "w");
  outPath = f ? path : std::string();
  return f;
}

void DMD::DumpDMDTxtThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  std::vector<uint8_t> frames[3] = {std::vector<uint8_t>(DMDUTIL_MAX_FRAME_PIXELS, 0),
                                    std::vector<uint8_t>(DMDUTIL_MAX_FRAME_PIXELS, 0),
                                    std::vector<uint8_t>(DMDUTIL_MAX_FRAME_PIXELS, 0)};
  uint32_t passed[3] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;
  std::string currentPath;
  std::unordered_set<uint64_t> seenHashes;

  Config* const pConfig = Config::GetInstance();
  const bool dumpNotColorizedFrames = pConfig->IsDumpNotColorizedFrames();
  const bool filterTransitionalFrames = pConfig->IsFilterTransitionalFrames();
  const bool dumpZip = pConfig->IsDumpZip();
  m_dumpTxtActive.store(true, std::memory_order_release);
  m_dumpTxtPosition.store(0, std::memory_order_release);
  m_dumpPositionCv.notify_all();

  OutputLoop(
      true, 0, nullptr,
      [&](uint8_t slot, uint16_t position, uint16_t)
      {
        m_dumpTxtPosition.store(position, std::memory_order_release);
        m_dumpPositionCv.notify_all();

        const Update& update = *m_pUpdateBufferQueue[slot];
        if (!(update.depth <= 4 && update.hasData &&
              ((update.mode == Mode::Data && !dumpNotColorizedFrames) ||
               (update.mode == Mode::NotColorized && dumpNotColorizedFrames))))
          return;

        bool newFile = false;
        if (strcmp(m_romName, name) != 0)
        {
          start = std::chrono::steady_clock::now();
          CloseDumpFile(f, currentPath, dumpZip);
          snprintf(name, sizeof(name), "%s", m_romName);

          if (name[0] != '\0')
          {
            f = OpenDumpFile(name, ".txt", currentPath);
            newFile = true;
            std::fill(frames[0].begin(), frames[0].end(), 0);
            std::fill(frames[1].begin(), frames[1].end(), 0);
            passed[0] = passed[1] = 0;
          }
        }

        if (name[0] == '\0') return;

        const size_t length = (size_t)update.width * update.height;
        if (length == 0 || length > DMDUTIL_MAX_FRAME_PIXELS) return;
        if (!newFile && memcmp(frames[1].data(), update.data, length) == 0) return;

        uint32_t queuedTimestamp = 0;
        if (GetQueueTimestamp(slot, queuedTimestamp))
          passed[2] = queuedTimestamp;
        else
          passed[2] =
              (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                             .count());
        memcpy(frames[2].data(), update.data, length);

        if (filterTransitionalFrames && update.depth == 2 &&
            (passed[2] - passed[1]) < DMDUTIL_MAX_TRANSITIONAL_FRAME_DURATION)
        {
          size_t i = 0;
          while (i < length &&
                 ((frames[0][i] == 2) || ((frames[0][i] == 3) || (frames[2][i] > 1)) == (frames[1][i] > 0)))
          {
            i++;
          }
          if (i == length)
          {
            Log(DMDUtil_LogLevel_DEBUG, "DumpDMDTxt: skip transitional frame");
            memcpy(frames[1].data(), frames[2].data(), length);
            passed[1] += passed[2];
            return;
          }
        }

        if (f && passed[0] > 0)
        {
          bool dump = true;

          if (dumpNotColorizedFrames)
          {
            const uint64_t hash = komihash(frames[0].data(), length, 0);
            if (!seenHashes.insert(hash).second)
            {
              Log(DMDUtil_LogLevel_DEBUG, "DumpDMDTxt: skip duplicate frame");
              dump = false;
            }
          }

          if (dump)
          {
            fprintf(f, "0x%08x\r\n", passed[0]);
            for (int y = 0; y < update.height; y++)
            {
              for (int x = 0; x < update.width; x++)
              {
                fprintf(f, "%x", frames[0][(size_t)y * update.width + x]);
              }
              fprintf(f, "\r\n");
            }
            fprintf(f, "\r\n");
          }
        }

        memcpy(frames[0].data(), frames[1].data(), length);
        passed[0] = passed[1];
        memcpy(frames[1].data(), frames[2].data(), length);
        passed[1] = passed[2];
      });

  CloseDumpFile(f, currentPath, dumpZip);
  m_dumpTxtActive.store(false, std::memory_order_release);
  m_dumpPositionCv.notify_all();
}

void DMD::DumpDMDColorThread(bool rgb888)
{
  std::atomic<bool>& active = rgb888 ? m_dump888Active : m_dump565Active;
  std::atomic<uint16_t>& dumpPosition = rgb888 ? m_dump888Position : m_dump565Position;
  const size_t bytesPerPixel = rgb888 ? 3 : 2;
  const char* extension = rgb888 ? ".888.txt" : ".565.txt";

  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  std::vector<uint8_t> frames[3] = {std::vector<uint8_t>(DMDUTIL_MAX_RGB24_BYTES, 0),
                                    std::vector<uint8_t>(DMDUTIL_MAX_RGB24_BYTES, 0),
                                    std::vector<uint8_t>(DMDUTIL_MAX_RGB24_BYTES, 0)};
  uint16_t frameWidths[3] = {0};
  uint16_t frameHeights[3] = {0};
  uint32_t passed[3] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;
  std::string currentPath;
  uint8_t palette[256 * 3] = {0};
  std::vector<uint8_t> rgb24Temp(DMDUTIL_MAX_RGB24_BYTES);

  const bool dumpZip = Config::GetInstance()->IsDumpZip();
  active.store(true, std::memory_order_release);
  dumpPosition.store(0, std::memory_order_release);
  m_dumpPositionCv.notify_all();

  OutputLoop(
      true, 0, nullptr,
      [&](uint8_t slot, uint16_t position, uint16_t)
      {
        dumpPosition.store(position, std::memory_order_release);
        m_dumpPositionCv.notify_all();

        const Update& update = *m_pUpdateBufferQueue[slot];
        if (!(update.hasData || update.hasSegData)) return;
        if (!(update.mode == Mode::RGB24 || update.mode == Mode::RGB16 || update.mode == Mode::SerumV1 ||
              update.mode == Mode::Vni || IsSerumV2Mode(update.mode)))
          return;

        bool updateFrame = false;
        if (strcmp(m_romName, name) != 0)
        {
          start = std::chrono::steady_clock::now();
          CloseDumpFile(f, currentPath, dumpZip);
          snprintf(name, sizeof(name), "%s", m_romName);

          if (name[0] != '\0')
          {
            f = OpenDumpFile(name, extension, currentPath);
            updateFrame = true;
            for (auto& frame : frames) std::fill(frame.begin(), frame.end(), 0);
            memset(frameWidths, 0, sizeof(frameWidths));
            memset(frameHeights, 0, sizeof(frameHeights));
            passed[0] = passed[1] = 0;
          }
        }

        if (name[0] == '\0') return;

        const uint16_t width = update.width;
        const uint16_t height = update.height;
        const size_t length = (size_t)width * height;
        const size_t frameBytes = length * bytesPerPixel;
        if (length == 0 || frameBytes > frames[0].size()) return;

        if (width != frameWidths[1] || height != frameHeights[1]) updateFrame = true;

        uint8_t* nextFrame = frames[2].data();
        if (rgb888)
        {
          ConvertColorUpdateToRgb24(update, palette, nextFrame);
        }
        else if (update.mode == Mode::RGB16 || IsSerumV2Mode(update.mode))
        {
          memcpy(nextFrame, update.segData, frameBytes);
        }
        else
        {
          ConvertColorUpdateToRgb24(update, palette, rgb24Temp.data());
          Rgb888ToRgb565(rgb24Temp.data(), (uint16_t*)nextFrame, length);
        }

        if (!updateFrame && memcmp(frames[1].data(), nextFrame, frameBytes) == 0) return;

        uint32_t queuedTimestamp = 0;
        if (GetQueueTimestamp(slot, queuedTimestamp))
          passed[2] = queuedTimestamp;
        else
          passed[2] =
              (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                             .count());
        frameWidths[2] = width;
        frameHeights[2] = height;

        if (f && passed[0] > 0 && frameWidths[0] > 0 && frameHeights[0] > 0)
        {
          fprintf(f, "0x%08x\r\n", passed[0]);
          const uint32_t rowWidth = frameWidths[0];
          const uint32_t rowHeight = frameHeights[0];
          const uint8_t* frame = frames[0].data();
          const uint16_t* frame565 = (const uint16_t*)frame;
          for (uint32_t y = 0; y < rowHeight; y++)
          {
            for (uint32_t x = 0; x < rowWidth; x++)
            {
              const size_t index = (size_t)y * rowWidth + x;
              if (rgb888)
                fprintf(f, "%02x%02x%02x", frame[index * 3], frame[index * 3 + 1], frame[index * 3 + 2]);
              else
                fprintf(f, "%04x", frame565[index]);
            }
            fprintf(f, "\r\n");
          }
          fprintf(f, "\r\n");
        }

        size_t prevBytes = (size_t)frameWidths[1] * frameHeights[1] * bytesPerPixel;
        if (prevBytes > frames[0].size()) prevBytes = frames[0].size();
        memcpy(frames[0].data(), frames[1].data(), prevBytes);
        passed[0] = passed[1];
        frameWidths[0] = frameWidths[1];
        frameHeights[0] = frameHeights[1];

        memcpy(frames[1].data(), nextFrame, frameBytes);
        passed[1] = passed[2];
        frameWidths[1] = frameWidths[2];
        frameHeights[1] = frameHeights[2];
      });

  CloseDumpFile(f, currentPath, dumpZip);
  active.store(false, std::memory_order_release);
  m_dumpPositionCv.notify_all();
}

void DMD::DumpDMDRawThread()
{
  char name[DMDUTIL_MAX_NAME_SIZE] = {0};
  std::chrono::steady_clock::time_point start;
  FILE* f = nullptr;

  m_dumpRawActive.store(true, std::memory_order_release);
  m_dumpRawPosition.store(0, std::memory_order_release);
  m_dumpPositionCv.notify_all();

  OutputLoop(true, 0, nullptr,
             [&](uint8_t slot, uint16_t position, uint16_t)
             {
               m_dumpRawPosition.store(position, std::memory_order_release);
               m_dumpPositionCv.notify_all();

               const Update& update = *m_pUpdateBufferQueue[slot];
               if (!(update.hasData || update.hasSegData)) return;

               if (strcmp(m_romName, name) != 0)
               {
                 start = std::chrono::steady_clock::now();
                 if (f)
                 {
                   fclose(f);
                   f = nullptr;
                 }
                 snprintf(name, sizeof(name), "%s", m_romName);

                 if (name[0] != '\0')
                 {
                   char filename[DMDUTIL_MAX_NAME_SIZE + 8];
                   snprintf(filename, sizeof(filename), "%s.raw", name);
                   f = fopen(filename, "ab");
                 }
               }

               if (name[0] == '\0' || !f) return;

               uint32_t current = 0;
               if (!GetQueueTimestamp(slot, current))
               {
                 current = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
               }
               fwrite(&current, 4, 1, f);

               const uint32_t size = sizeof(Update);
               fwrite(&size, 4, 1, f);
               fwrite(&update, 1, size, f);
             });

  if (f)
  {
    fflush(f);
    fclose(f);
  }
  m_dumpRawActive.store(false, std::memory_order_release);
  m_dumpPositionCv.notify_all();
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
  if (m_pSerum && source == 'D' && value == 1)
  {
    auto commandUpdate = std::make_shared<Update>();
    commandUpdate->mode = Mode::SerumCommand;
    commandUpdate->hasData = true;
    commandUpdate->hasSegData = true;
    commandUpdate->hasSegData2 = false;
    commandUpdate->data[0] = static_cast<uint8_t>(source);
    commandUpdate->data[1] = value;
    commandUpdate->segData[0] = event;

    QueueUpdate(commandUpdate, false);
  }
}

}  // namespace DMDUtil
