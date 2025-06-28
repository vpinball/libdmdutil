#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "DMDUtil/DMD.h"
#include "mongoose/mongoose.h"

namespace DMDUtil
{
class DMDUTILAPI VirtualDMD : public DMD
{
 public:
  VirtualDMD(int port = 6790, const char* bindAddr = "localhost");
  ~VirtualDMD();

  bool StartWebServer();
  void StopWebServer();
  bool IsWebServerRunning() const { return m_webServerRunning; }

  bool HasDisplay() const { return m_webServerRunning; }

  void UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, bool buffered = false);
  void UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height, bool buffered = false);
  void UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b,
                  bool buffered = false);
  void QueueUpdate(const std::shared_ptr<Update> dmdUpdate, bool buffered);

  static void EventHandler(struct mg_connection* c, int ev, void* ev_data);

 private:
  void ProcessWebServerEvents();
  void BroadcastFrameData(const uint8_t* pData, uint16_t width, uint16_t height);
  void SendIndexPage(struct mg_connection* c);

  struct mg_mgr m_mgr;
  std::atomic<bool> m_webServerRunning{false};
  std::thread m_webServerThread;
  int m_port;
  std::string m_bindAddr;

  std::mutex m_frameMutex;
  std::vector<uint8_t> m_currentFrame;
  uint16_t m_currentWidth = 0;
  uint16_t m_currentHeight = 0;
  bool m_frameUpdated = false;

  static VirtualDMD* s_instance;
};
}  // namespace DMDUtil