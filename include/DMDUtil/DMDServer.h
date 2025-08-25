#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "DMDUtil/DMD.h"
#include "sockpp/tcp_acceptor.h"

#define DMDSERVER_MAX_WIDTH 256
#define DMDSERVER_MAX_HEIGHT 64

namespace DMDUtil
{

class DMDUTILAPI DMDServer
{
 public:
  DMDServer(DMD* dmd, bool fixedAltColorPath = false, bool fixedPupPath = false);
  ~DMDServer();

  bool Start(const char* addr, in_port_t port);
  void Stop();
  bool IsRunning() const { return m_running.load(std::memory_order_acquire); }

 private:
  void AcceptLoop();
  void ClientThread(sockpp::tcp_socket sock, uint32_t threadId);

  DMD* m_dmd;
  bool m_fixedAltColorPath;
  bool m_fixedPupPath;
  std::atomic<bool> m_running{false};
  sockpp::tcp_acceptor m_acceptor;

  uint32_t m_currentThreadId{0};
  std::mutex m_threadMutex;
  uint32_t m_disconnectOtherClients{0};
  std::vector<uint32_t> m_threads;
  std::thread* m_acceptThread{nullptr};
};

}  // namespace DMDUtil
