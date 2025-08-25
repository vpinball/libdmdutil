#include "DMDUtil/DMDServer.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>  // Windows byte-order functions
#else
#include <arpa/inet.h>  // Linux/macOS byte-order functions
#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

#include "DMDUtil/DMD.h"
#include "Logger.h"
#include "sockpp/tcp_acceptor.h"

namespace DMDUtil
{

DMDServer::DMDServer(DMD* dmd, bool fixedAltColorPath, bool fixedPupPath)
    : m_dmd(dmd), m_fixedAltColorPath(fixedAltColorPath), m_fixedPupPath(fixedPupPath)
{
}

DMDServer::~DMDServer() { Stop(); }

bool DMDServer::Start(const char* addr, in_port_t port)
{
  if (m_running.load(std::memory_order_acquire) || !m_dmd->HasDisplay()) return false;

  sockpp::initialize();
  m_acceptor = sockpp::tcp_acceptor({addr, port});
  if (!m_acceptor)
  {
    Log(DMDUtil_LogLevel_ERROR, "Error creating DMDServer acceptor: %s", m_acceptor.last_error_str().c_str());
    return false;
  }

  m_acceptor.set_non_blocking();

  m_running.store(true, std::memory_order_release);
  m_acceptThread = new std::thread(&DMDServer::AcceptLoop, this);
  return true;
}

void DMDServer::Stop()
{
  m_running.store(false, std::memory_order_release);

  if (m_acceptThread)
  {
    m_acceptThread->join();
    delete m_acceptThread;
    m_acceptThread = nullptr;
  }

  std::unique_lock<std::mutex> lock(m_threadMutex);
  m_currentThreadId = 0;
  m_disconnectOtherClients = 0;
  lock.unlock();
}

void DMDServer::AcceptLoop()
{
  uint32_t threadId = 0;

  while (m_running.load(std::memory_order_relaxed))
  {
    sockpp::inet_address peer;
    sockpp::tcp_socket sock = m_acceptor.accept(&peer);

    if (!sock)
    {
      if (m_acceptor.last_error() == EWOULDBLOCK)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      Log(DMDUtil_LogLevel_ERROR, "Error accepting connection: %s", m_acceptor.last_error_str().c_str());
      continue;
    }

    std::unique_lock<std::mutex> lock(m_threadMutex);
    m_currentThreadId = ++threadId;
    m_threads.push_back(m_currentThreadId);
    lock.unlock();

    std::thread thr(&DMDServer::ClientThread, this, std::move(sock), m_currentThreadId);
    thr.detach();
  }
}

void DMDServer::ClientThread(sockpp::tcp_socket sock, uint32_t threadId)
{
  // Socket auf non-blocking setzen
  sock.set_non_blocking();

  uint8_t buffer[sizeof(DMDUtil::DMD::Update)];
  DMDUtil::DMD::StreamHeader* pStreamHeader = (DMDUtil::DMD::StreamHeader*)malloc(sizeof(DMDUtil::DMD::StreamHeader));
  ssize_t n;
  bool handleDisconnectOthers = true;
  bool logged = false;

  DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: New DMD client %d connected", threadId, threadId);

  while (m_running.load(std::memory_order_relaxed) &&
         (threadId == m_currentThreadId || m_disconnectOtherClients == 0 || m_disconnectOtherClients <= threadId))
  {
    n = sock.read_n(buffer, sizeof(DMDUtil::DMD::StreamHeader));

    if (n < 0)
    {
      if (sock.last_error() == EWOULDBLOCK)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      // network error or connection closed
      break;
    }
    else if (n == 0)
    {
      // connection closed by client
      break;
    }
    else if (n == sizeof(DMDUtil::DMD::StreamHeader))
    {
      memcpy(pStreamHeader, buffer, n);
      pStreamHeader->convertToHostByteOrder();
      if (strcmp(pStreamHeader->header, "DMDStream") == 0 && pStreamHeader->version == 1)
      {
        DMDUtil::Log(DMDUtil_LogLevel_DEBUG, "%d: Received DMDStream header version %d for DMD mode %d", threadId,
                     pStreamHeader->version, pStreamHeader->mode);
        if (pStreamHeader->buffered && threadId == m_currentThreadId)
          DMDUtil::Log(DMDUtil_LogLevel_DEBUG, "%d: Next data will be buffered", threadId);

        // Only the current (most recent) thread is allowed to disconnect other clients.
        if (handleDisconnectOthers && threadId == m_currentThreadId && pStreamHeader->disconnectOthers)
        {
          m_threadMutex.lock();
          m_disconnectOtherClients = threadId;
          m_threadMutex.unlock();
          handleDisconnectOthers = false;
          DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Other clients will be disconnected", threadId);
        }

        switch (pStreamHeader->mode)
        {
          case DMDUtil::DMD::Mode::Data:
            if ((n = sock.read_n(buffer, sizeof(DMDUtil::DMD::PathsHeader))) == sizeof(DMDUtil::DMD::PathsHeader))
            {
              DMDUtil::DMD::PathsHeader pathsHeader;
              memcpy(&pathsHeader, buffer, n);
              pathsHeader.convertToHostByteOrder();

              if (strcmp(pathsHeader.header, "Paths") == 0 &&
                  (n = sock.read_n(buffer, sizeof(DMDUtil::DMD::Update))) == sizeof(DMDUtil::DMD::Update) &&
                  threadId == m_currentThreadId)
              {
                DMDUtil::Log(DMDUtil_LogLevel_DEBUG,
                             "%d: Received paths header: ROM '%s', AltColorPath '%s', PupPath '%s'", threadId,
                             pathsHeader.name, pathsHeader.altColorPath, pathsHeader.pupVideosPath);
                auto data = std::make_shared<DMDUtil::DMD::Update>();
                memcpy(data.get(), buffer, n);
                data->convertToHostByteOrder();
                logged = false;

                if (data->width <= DMDSERVER_MAX_WIDTH && data->height <= DMDSERVER_MAX_HEIGHT)
                {
                  m_dmd->SetRomName(pathsHeader.name);
                  if (!m_fixedAltColorPath) m_dmd->SetAltColorPath(pathsHeader.altColorPath);
                  if (!m_fixedPupPath) m_dmd->SetPUPVideosPath(pathsHeader.pupVideosPath);

                  m_dmd->QueueUpdate(data, (pStreamHeader->buffered == 1));
                }
                else
                {
                  DMDUtil::Log(DMDUtil_LogLevel_ERROR, "%d: TCP data package is missing or corrupted!", threadId);
                }
              }
              else if (threadId != m_currentThreadId)
              {
                if (!logged)
                {
                  DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Client %d blocks the DMD", threadId, m_currentThreadId);
                  logged = true;
                }
                DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Client %d blocks the DMD", threadId, m_currentThreadId);
              }
              else
              {
                DMDUtil::Log(DMDUtil_LogLevel_ERROR, "%d: Paths header is missing!", threadId);
              }
            }
            break;

          case DMDUtil::DMD::Mode::RGB16:
            if ((n = sock.read_n(buffer, pStreamHeader->length)) == pStreamHeader->length &&
                threadId == m_currentThreadId && pStreamHeader->width <= DMDSERVER_MAX_WIDTH &&
                pStreamHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              uint16_t* pixelData = (uint16_t*)buffer;
              size_t pixelCount = pStreamHeader->length / sizeof(uint16_t);
              for (size_t i = 0; i < pixelCount; i++)
              {
                pixelData[i] = ntohs(pixelData[i]);
              }
              logged = false;
              m_dmd->UpdateRGB16Data((uint16_t*)buffer, pStreamHeader->width, pStreamHeader->height,
                                     pStreamHeader->buffered == 1);
            }
            else if (threadId != m_currentThreadId)
            {
              if (!logged)
              {
                DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Client %d blocks the DMD", threadId, m_currentThreadId);
                logged = true;
              }
            }
            else
            {
              DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: TCP data package is missing or corrupted!", threadId);
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pStreamHeader->length)) == pStreamHeader->length &&
                threadId == m_currentThreadId && pStreamHeader->width <= DMDSERVER_MAX_WIDTH &&
                pStreamHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              logged = false;
              m_dmd->UpdateRGB24Data(buffer, pStreamHeader->width, pStreamHeader->height, pStreamHeader->buffered == 1);
            }
            else if (threadId != m_currentThreadId)
            {
              if (!logged)
              {
                DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Client %d blocks the DMD", threadId, m_currentThreadId);
                logged = true;
              }
            }
            else
            {
              DMDUtil::Log(DMDUtil_LogLevel_ERROR, "%d: TCP data package is missing or corrupted!", threadId);
            }
            break;

          default:
            // Other modes aren't supported via network.
            break;
        }
      }
      else if (threadId == m_currentThreadId)
      {
        DMDUtil::Log(DMDUtil_LogLevel_DEBUG, "%d: Received unknown TCP package", threadId);
      }
    }
  }

  if (m_disconnectOtherClients != 0 && m_disconnectOtherClients > threadId)
    DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Client %d requested disconnect", threadId, m_disconnectOtherClients);

  // Display a buffered frame or clear the display on disconnect of the current thread.
  if (threadId == m_currentThreadId && !pStreamHeader->buffered && !m_dmd->QueueBuffer())
  {
    m_dmd->SetRomName("");
    DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: Clear screen on disconnect", threadId);
    // Clear the DMD by sending a black screen.
    // Fixed dimension of 128x32 should be OK for all devices.
    memset(buffer, 0, 128 * 32 * 3);
    m_dmd->UpdateRGB24Data(buffer, 128, 32, true);
  }

  m_threadMutex.lock();
  m_threads.erase(remove(m_threads.begin(), m_threads.end(), threadId), m_threads.end());
  if (threadId == m_currentThreadId)
  {
    if (m_disconnectOtherClients == threadId)
    {
      // Wait until all other threads ended or a new client connnects in between.
      while (m_threads.size() >= 1 && m_currentThreadId == threadId)
      {
        m_threadMutex.unlock();
        // Let other threads terminate.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        m_threadMutex.lock();
      }

      m_currentThreadId = 0;
      m_disconnectOtherClients = 0;
    }
    else
    {
      m_currentThreadId = (m_threads.size() >= 1) ? m_threads.back() : 0;
    }

    DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: DMD client %d set as current", threadId, m_currentThreadId);
  }
  m_threadMutex.unlock();

  DMDUtil::Log(DMDUtil_LogLevel_INFO, "%d: DMD client %d disconnected", threadId, threadId);

  free(pStreamHeader);
}

}  // namespace DMDUtil
