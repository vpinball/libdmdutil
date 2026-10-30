/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Network/DMDExtStream.cs
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Network/WebsocketSerializer.cs
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sockpp/tcp_connector.h"

#define DMDEXTSTREAM_WIDTH 128
#define DMDEXTSTREAM_HEIGHT 32
#define DMDEXTSTREAM_CONNECT_TIMEOUT_MS 2000
#define DMDEXTSTREAM_HANDSHAKE_TIMEOUT_MS 3000
#define DMDEXTSTREAM_MAX_INBOUND_PAYLOAD (1 << 20)

namespace DMDUtil
{

class DMDExtStream
{
 public:
  static DMDExtStream* Create(const char* pUrl, bool retry, int retryIntervalSeconds);
  ~DMDExtStream();

  int GetWidth() const { return DMDEXTSTREAM_WIDTH; }
  int GetHeight() const { return DMDEXTSTREAM_HEIGHT; }
  bool IsConnected() const { return m_connected.load(std::memory_order_acquire); }
  bool IsRetryEnabled() const { return m_retry; }
  bool EnsureConnected();

  void SetGameName(const char* pGameName);
  void SendColor(uint8_t r, uint8_t g, uint8_t b);
  void SendGray(const uint8_t* pFrame, int bitLength);
  void SendColoredGray4(const uint8_t* pFrame, const uint8_t* pPalette);
  void SendRgb24(const uint8_t* pFrame);

 private:
  DMDExtStream(const std::string& host, uint16_t port, const std::string& path, bool retry, int retryIntervalSeconds);
  static bool ParseUrl(const char* pUrl, std::string& host, uint16_t& port, std::string& path);
  bool Connect();
  void Disconnect();
  bool Handshake();
  void OnOpen();
  void ReaderThread();
  bool SendFrame(uint8_t opcode, const uint8_t* pData, size_t length);
  bool SendCurrentMessage();
  void BeginMessage(const char* pName);
  void AppendU32(uint32_t value);
  void AppendBytes(const uint8_t* pData, size_t length);
  uint32_t Timestamp() const;
  uint32_t NextRandom();

  std::string m_host;
  uint16_t m_port;
  std::string m_path;
  bool m_retry;
  int m_retryIntervalMs;
  std::chrono::steady_clock::time_point m_startedAt;
  std::chrono::steady_clock::time_point m_lastAttempt;
  bool m_hasAttempted;
  sockpp::tcp_connector* m_pConnector;
  std::atomic<bool> m_connected;
  std::atomic<bool> m_readerRunning;
  std::thread* m_pReaderThread;
  std::mutex m_writeMutex;
  std::vector<uint8_t> m_message;
  std::vector<uint8_t> m_wsFrame;
  std::vector<uint8_t> m_planes;
  std::vector<uint8_t> m_lastFrame;
  std::string m_gameName;
  std::string m_sentGameName;
  uint8_t m_color[3];
  bool m_hasColor;
  bool m_lastSendFailed;
  uint32_t m_randomState;
};

}  // namespace DMDUtil
