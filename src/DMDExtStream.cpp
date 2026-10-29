/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Network/DMDExtStream.cs
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Network/WebsocketSerializer.cs
 */

#include "DMDExtStream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <random>

#include "DMDUtil/Logger.h"
#include "FrameUtil.h"
#include "sockpp/tcp_connector.h"

namespace DMDUtil
{

namespace
{

constexpr uint8_t kOpcodeText = 0x1;
constexpr uint8_t kOpcodeBinary = 0x2;
constexpr uint8_t kOpcodeClose = 0x8;
constexpr uint8_t kOpcodePing = 0x9;
constexpr uint8_t kOpcodePong = 0xA;

const char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

void Sha1(const uint8_t* pData, size_t length, uint8_t* pDigest)
{
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

  std::vector<uint8_t> msg(pData, pData + length);
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  uint64_t bitLength = (uint64_t)length * 8;
  for (int i = 7; i >= 0; i--) msg.push_back((uint8_t)(bitLength >> (i * 8)));

  for (size_t chunk = 0; chunk < msg.size(); chunk += 64)
  {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
    {
      w[i] = ((uint32_t)msg[chunk + i * 4] << 24) | ((uint32_t)msg[chunk + i * 4 + 1] << 16) |
             ((uint32_t)msg[chunk + i * 4 + 2] << 8) | (uint32_t)msg[chunk + i * 4 + 3];
    }
    for (int i = 16; i < 80; i++)
    {
      uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (v << 1) | (v >> 31);
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++)
    {
      uint32_t f, k;
      if (i < 20)
      {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      }
      else if (i < 40)
      {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      }
      else if (i < 60)
      {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      }
      else
      {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
      e = d;
      d = c;
      c = (b << 30) | (b >> 2);
      b = a;
      a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  for (int i = 0; i < 5; i++)
  {
    pDigest[i * 4] = (uint8_t)(h[i] >> 24);
    pDigest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    pDigest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    pDigest[i * 4 + 3] = (uint8_t)h[i];
  }
}

std::string Base64(const uint8_t* pData, size_t length)
{
  static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  while (i + 2 < length)
  {
    uint32_t v = ((uint32_t)pData[i] << 16) | ((uint32_t)pData[i + 1] << 8) | pData[i + 2];
    out += kTable[(v >> 18) & 0x3F];
    out += kTable[(v >> 12) & 0x3F];
    out += kTable[(v >> 6) & 0x3F];
    out += kTable[v & 0x3F];
    i += 3;
  }
  if (i < length)
  {
    uint32_t v = (uint32_t)pData[i] << 16;
    if (i + 1 < length) v |= (uint32_t)pData[i + 1] << 8;
    out += kTable[(v >> 18) & 0x3F];
    out += kTable[(v >> 12) & 0x3F];
    out += (i + 1 < length) ? kTable[(v >> 6) & 0x3F] : '=';
    out += '=';
  }
  return out;
}

std::string ToLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
  return s;
}

std::string Trim(const std::string& s)
{
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

DMDExtStream::DMDExtStream(const std::string& host, uint16_t port, const std::string& path, bool retry,
                           int retryIntervalSeconds)
    : m_host(host),
      m_port(port),
      m_path(path),
      m_retry(retry),
      m_retryIntervalMs(retryIntervalSeconds < 1 ? 1000 : retryIntervalSeconds * 1000),
      m_startedAt(std::chrono::steady_clock::now()),
      m_lastAttempt(m_startedAt),
      m_hasAttempted(false),
      m_pConnector(nullptr),
      m_connected(false),
      m_readerRunning(false),
      m_pReaderThread(nullptr),
      m_hasColor(false),
      m_lastSendFailed(false)
{
  m_color[0] = m_color[1] = m_color[2] = 0;
  m_planes.resize(DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT / 8 * 4);
  m_message.reserve(DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT * 3 + 64);
  m_wsFrame.reserve(DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT * 3 + 128);
  std::random_device rd;
  m_randomState = rd() | 1u;
}

DMDExtStream::~DMDExtStream() { Disconnect(); }

DMDExtStream* DMDExtStream::Create(const char* pUrl, bool retry, int retryIntervalSeconds)
{
  std::string host;
  uint16_t port = 80;
  std::string path;

  if (!ParseUrl(pUrl, host, port, path))
  {
    Log(DMDUtil_LogLevel_ERROR, "DMDExtStream: invalid WebSocket URL '%s', expected ws://host[:port][/path]",
        pUrl ? pUrl : "");
    return nullptr;
  }

  DMDExtStream* pStream = new DMDExtStream(host, port, path, retry, retryIntervalSeconds);

  Log(DMDUtil_LogLevel_INFO, "DMDExtStream: attempting to connect to WebSocket at ws://%s:%u%s", host.c_str(), port,
      path.c_str());

  if (!pStream->Connect() && !retry)
  {
    delete pStream;
    return nullptr;
  }

  return pStream;
}

bool DMDExtStream::ParseUrl(const char* pUrl, std::string& host, uint16_t& port, std::string& path)
{
  if (!pUrl || !*pUrl) return false;

  std::string url = Trim(pUrl);
  std::string lower = ToLower(url);

  if (lower.rfind("ws://", 0) == 0)
    url = url.substr(5);
  else if (lower.rfind("wss://", 0) == 0)
    return false;
  else if (url.find("://") != std::string::npos)
    return false;

  size_t slash = url.find('/');
  std::string authority = (slash == std::string::npos) ? url : url.substr(0, slash);
  path = (slash == std::string::npos) ? "/" : url.substr(slash);

  if (authority.empty()) return false;

  size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos)
  {
    host = authority.substr(0, colon);
    std::string portStr = authority.substr(colon + 1);
    if (portStr.empty() || portStr.find_first_not_of("0123456789") != std::string::npos) return false;
    long value = strtol(portStr.c_str(), nullptr, 10);
    if (value < 1 || value > 65535) return false;
    port = (uint16_t)value;
  }
  else
  {
    host = authority;
    port = 80;
  }

  return !host.empty();
}

uint32_t DMDExtStream::NextRandom()
{
  uint32_t x = m_randomState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  m_randomState = x;
  return x;
}

uint32_t DMDExtStream::Timestamp() const
{
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startedAt)
      .count();
}

bool DMDExtStream::Connect()
{
  Disconnect();

  m_hasAttempted = true;
  m_lastAttempt = std::chrono::steady_clock::now();

  sockpp::initialize();

  sockpp::tcp_connector* pConnector = new sockpp::tcp_connector();

  try
  {
    sockpp::inet_address addr(m_host, m_port);
    if (!pConnector->connect(addr, std::chrono::milliseconds(DMDEXTSTREAM_CONNECT_TIMEOUT_MS)))
    {
      Log(DMDUtil_LogLevel_INFO, "DMDExtStream: connection to %s:%u failed: %s", m_host.c_str(), m_port,
          pConnector->last_error_str().c_str());
      delete pConnector;
      return false;
    }
  }
  catch (const std::exception& e)
  {
    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: unable to resolve %s: %s", m_host.c_str(), e.what());
    delete pConnector;
    return false;
  }

  m_pConnector = pConnector;

  if (!Handshake())
  {
    m_pConnector->close();
    delete m_pConnector;
    m_pConnector = nullptr;
    return false;
  }

  m_connected.store(true, std::memory_order_release);
  m_lastSendFailed = false;
  m_readerRunning.store(true, std::memory_order_release);
  m_pReaderThread = new std::thread(&DMDExtStream::ReaderThread, this);

  Log(DMDUtil_LogLevel_INFO, "DMDExtStream: connected to WebSocket at ws://%s:%u%s", m_host.c_str(), m_port,
      m_path.c_str());

  OnOpen();

  return true;
}

void DMDExtStream::Disconnect()
{
  m_readerRunning.store(false, std::memory_order_release);

  if (m_pConnector)
  {
    if (m_connected.load(std::memory_order_acquire))
    {
      uint8_t reason[2] = {0x03, 0xE8};
      SendFrame(kOpcodeClose, reason, sizeof(reason));
    }
    m_connected.store(false, std::memory_order_release);
    m_pConnector->shutdown();
  }

  m_connected.store(false, std::memory_order_release);

  if (m_pReaderThread)
  {
    m_pReaderThread->join();
    delete m_pReaderThread;
    m_pReaderThread = nullptr;
  }

  if (m_pConnector)
  {
    m_pConnector->close();
    delete m_pConnector;
    m_pConnector = nullptr;
  }

  m_sentGameName.clear();
}

bool DMDExtStream::EnsureConnected()
{
  if (m_connected.load(std::memory_order_acquire)) return true;

  if (m_pConnector) Disconnect();

  if (!m_retry) return false;

  if (m_hasAttempted)
  {
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_lastAttempt).count();
    if (elapsed < m_retryIntervalMs) return false;

    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: retrying connection to WebSocket at ws://%s:%u%s", m_host.c_str(), m_port,
        m_path.c_str());
  }

  return Connect();
}

bool DMDExtStream::Handshake()
{
  uint8_t nonce[16];
  for (int i = 0; i < 16; i += 4)
  {
    uint32_t r = NextRandom();
    nonce[i] = (uint8_t)r;
    nonce[i + 1] = (uint8_t)(r >> 8);
    nonce[i + 2] = (uint8_t)(r >> 16);
    nonce[i + 3] = (uint8_t)(r >> 24);
  }
  std::string key = Base64(nonce, sizeof(nonce));

  std::string request = "GET " + m_path + " HTTP/1.1\r\n";
  request += "Host: " + m_host + ":" + std::to_string(m_port) + "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: " + key + "\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += "\r\n";

  if (m_pConnector->write_n(request.data(), request.size()) != (ssize_t)request.size())
  {
    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: failed to send WebSocket handshake: %s",
        m_pConnector->last_error_str().c_str());
    return false;
  }

  m_pConnector->read_timeout(std::chrono::milliseconds(DMDEXTSTREAM_HANDSHAKE_TIMEOUT_MS));

  std::string response;
  while (response.size() < 8192)
  {
    char c;
    if (m_pConnector->read(&c, 1) != 1)
    {
      Log(DMDUtil_LogLevel_INFO, "DMDExtStream: no WebSocket handshake response from %s:%u", m_host.c_str(), m_port);
      return false;
    }
    response += c;
    if (response.size() >= 4 && response.compare(response.size() - 4, 4, "\r\n\r\n") == 0) break;
  }

  m_pConnector->read_timeout(std::chrono::microseconds(0));

  size_t lineEnd = response.find("\r\n");
  std::string statusLine = response.substr(0, lineEnd);
  if (statusLine.find(" 101") == std::string::npos)
  {
    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: WebSocket upgrade rejected by %s:%u: %s", m_host.c_str(), m_port,
        statusLine.c_str());
    return false;
  }

  std::string accept;
  size_t pos = lineEnd + 2;
  while (pos < response.size())
  {
    size_t next = response.find("\r\n", pos);
    if (next == std::string::npos) break;
    std::string line = response.substr(pos, next - pos);
    pos = next + 2;
    size_t sep = line.find(':');
    if (sep == std::string::npos) continue;
    if (ToLower(Trim(line.substr(0, sep))) == "sec-websocket-accept") accept = Trim(line.substr(sep + 1));
  }

  std::string material = key + kWebSocketGuid;
  uint8_t digest[20];
  Sha1((const uint8_t*)material.data(), material.size(), digest);
  std::string expected = Base64(digest, sizeof(digest));

  if (accept != expected)
  {
    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: WebSocket handshake with %s:%u returned a bad accept key", m_host.c_str(),
        m_port);
    return false;
  }

  return true;
}

void DMDExtStream::OnOpen()
{
  if (!m_gameName.empty())
  {
    BeginMessage("gameName");
    AppendBytes((const uint8_t*)m_gameName.c_str(), m_gameName.size() + 1);
    SendCurrentMessage();
    m_sentGameName = m_gameName;
  }

  BeginMessage("dimensions");
  AppendU32(DMDEXTSTREAM_WIDTH);
  AppendU32(DMDEXTSTREAM_HEIGHT);
  SendCurrentMessage();

  if (m_hasColor)
  {
    BeginMessage("color");
    AppendU32(((uint32_t)m_color[0] << 16) | ((uint32_t)m_color[1] << 8) | m_color[2]);
    SendCurrentMessage();
  }

  if (!m_lastFrame.empty()) SendFrame(kOpcodeBinary, m_lastFrame.data(), m_lastFrame.size());
}

void DMDExtStream::ReaderThread()
{
  std::vector<uint8_t> payload;

  while (m_readerRunning.load(std::memory_order_acquire))
  {
    uint8_t header[2];
    if (m_pConnector->read_n(header, 2) != 2) break;

    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;

    if (length == 126)
    {
      uint8_t ext[2];
      if (m_pConnector->read_n(ext, 2) != 2) break;
      length = ((uint64_t)ext[0] << 8) | ext[1];
    }
    else if (length == 127)
    {
      uint8_t ext[8];
      if (m_pConnector->read_n(ext, 8) != 8) break;
      length = 0;
      for (int i = 0; i < 8; i++) length = (length << 8) | ext[i];
    }

    uint8_t mask[4] = {0};
    if (masked && m_pConnector->read_n(mask, 4) != 4) break;

    if (length > DMDEXTSTREAM_MAX_INBOUND_PAYLOAD) break;

    payload.resize((size_t)length);
    if (length > 0 && m_pConnector->read_n(payload.data(), (size_t)length) != (ssize_t)length) break;

    if (masked)
    {
      for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i % 4];
    }

    if (opcode == kOpcodeClose)
    {
      Log(DMDUtil_LogLevel_INFO, "DMDExtStream: WebSocket connection was closed by the server");
      break;
    }
    else if (opcode == kOpcodePing)
    {
      SendFrame(kOpcodePong, payload.data(), payload.size());
    }
    else if (opcode == kOpcodeText)
    {
      Log(DMDUtil_LogLevel_DEBUG, "DMDExtStream: message from server: %.*s", (int)payload.size(),
          (const char*)payload.data());
    }
  }

  if (m_readerRunning.load(std::memory_order_acquire) && m_connected.load(std::memory_order_acquire))
    Log(DMDUtil_LogLevel_INFO, "DMDExtStream: WebSocket connection to %s:%u lost", m_host.c_str(), m_port);

  m_connected.store(false, std::memory_order_release);
}

bool DMDExtStream::SendFrame(uint8_t opcode, const uint8_t* pData, size_t length)
{
  std::lock_guard<std::mutex> lock(m_writeMutex);

  if (!m_pConnector || !m_connected.load(std::memory_order_acquire)) return false;

  m_wsFrame.clear();
  m_wsFrame.push_back((uint8_t)(0x80 | opcode));

  if (length < 126)
  {
    m_wsFrame.push_back((uint8_t)(0x80 | length));
  }
  else if (length < 65536)
  {
    m_wsFrame.push_back(0x80 | 126);
    m_wsFrame.push_back((uint8_t)(length >> 8));
    m_wsFrame.push_back((uint8_t)length);
  }
  else
  {
    m_wsFrame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; i--) m_wsFrame.push_back((uint8_t)((uint64_t)length >> (i * 8)));
  }

  uint32_t r = NextRandom();
  uint8_t mask[4] = {(uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24)};
  m_wsFrame.insert(m_wsFrame.end(), mask, mask + 4);

  size_t offset = m_wsFrame.size();
  m_wsFrame.resize(offset + length);
  for (size_t i = 0; i < length; i++) m_wsFrame[offset + i] = pData[i] ^ mask[i % 4];

  if (m_pConnector->write_n(m_wsFrame.data(), m_wsFrame.size()) != (ssize_t)m_wsFrame.size())
  {
    if (!m_lastSendFailed)
    {
      Log(DMDUtil_LogLevel_INFO, "DMDExtStream: error writing to WebSocket: %s",
          m_pConnector->last_error_str().c_str());
      m_lastSendFailed = true;
    }
    m_connected.store(false, std::memory_order_release);
    return false;
  }

  return true;
}

void DMDExtStream::BeginMessage(const char* pName)
{
  m_message.clear();
  m_message.insert(m_message.end(), pName, pName + strlen(pName));
  m_message.push_back(0);
}

void DMDExtStream::AppendU32(uint32_t value)
{
  m_message.push_back((uint8_t)value);
  m_message.push_back((uint8_t)(value >> 8));
  m_message.push_back((uint8_t)(value >> 16));
  m_message.push_back((uint8_t)(value >> 24));
}

void DMDExtStream::AppendBytes(const uint8_t* pData, size_t length)
{
  m_message.insert(m_message.end(), pData, pData + length);
}

bool DMDExtStream::SendCurrentMessage() { return SendFrame(kOpcodeBinary, m_message.data(), m_message.size()); }

void DMDExtStream::SetGameName(const char* pGameName)
{
  std::string name = pGameName ? pGameName : "";
  if (name == m_gameName) return;

  m_gameName = name;

  if (m_connected.load(std::memory_order_acquire) && !m_gameName.empty() && m_gameName != m_sentGameName)
  {
    BeginMessage("gameName");
    AppendBytes((const uint8_t*)m_gameName.c_str(), m_gameName.size() + 1);
    if (SendCurrentMessage()) m_sentGameName = m_gameName;
  }
}

void DMDExtStream::SendColor(uint8_t r, uint8_t g, uint8_t b)
{
  if (m_hasColor && m_color[0] == r && m_color[1] == g && m_color[2] == b) return;

  m_color[0] = r;
  m_color[1] = g;
  m_color[2] = b;
  m_hasColor = true;

  if (!m_connected.load(std::memory_order_acquire)) return;

  BeginMessage("color");
  AppendU32(((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
  SendCurrentMessage();
}

void DMDExtStream::SendGray(const uint8_t* pFrame, int bitLength)
{
  if (!m_connected.load(std::memory_order_acquire)) return;
  if (bitLength != 2 && bitLength != 4) return;

  const size_t planeSize = DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT / 8;
  FrameUtil::Helper::Split(m_planes.data(), DMDEXTSTREAM_WIDTH, DMDEXTSTREAM_HEIGHT, (uint8_t)bitLength,
                           (uint8_t*)pFrame);

  BeginMessage(bitLength == 2 ? "gray2Planes" : "gray4Planes");
  AppendU32(Timestamp());
  AppendBytes(m_planes.data(), planeSize * bitLength);
  m_lastFrame = m_message;
  SendCurrentMessage();
}

void DMDExtStream::SendColoredGray4(const uint8_t* pFrame, const uint8_t* pPalette)
{
  if (!m_connected.load(std::memory_order_acquire)) return;

  const size_t planeSize = DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT / 8;
  FrameUtil::Helper::Split(m_planes.data(), DMDEXTSTREAM_WIDTH, DMDEXTSTREAM_HEIGHT, 4, (uint8_t*)pFrame);

  BeginMessage("coloredGray4");
  AppendU32(Timestamp());
  AppendU32(16);
  for (int i = 0; i < 16; i++)
  {
    AppendU32(((uint32_t)pPalette[i * 3] << 16) | ((uint32_t)pPalette[i * 3 + 1] << 8) | pPalette[i * 3 + 2]);
  }
  AppendBytes(m_planes.data(), planeSize * 4);
  m_lastFrame = m_message;
  SendCurrentMessage();
}

void DMDExtStream::SendRgb24(const uint8_t* pFrame)
{
  if (!m_connected.load(std::memory_order_acquire)) return;

  BeginMessage("rgb24");
  AppendU32(Timestamp());
  AppendBytes(pFrame, DMDEXTSTREAM_WIDTH * DMDEXTSTREAM_HEIGHT * 3);
  m_lastFrame = m_message;
  SendCurrentMessage();
}

}  // namespace DMDUtil
