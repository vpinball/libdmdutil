#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// clang-format off
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <execinfo.h>
#include <mach/mach.h>
#include <unistd.h>
#elif defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#endif
// clang-format on

#include "DMDUtil/DMDUtil.h"
#include "cargs.h"
#include "miniz/miniz.h"
#include "serum.h"

namespace
{
std::atomic<bool> g_stopRequested{false};
volatile sig_atomic_t g_crashHandlerActive = 0;
constexpr size_t kCrashAltStackSize = 65536;
uint8_t g_crashAltStack[kCrashAltStackSize];

void HandleSigInt(int) { g_stopRequested.store(true, std::memory_order_release); }

void DMDUTILCALLBACK LogToStdoutCallback(DMDUtil_LogLevel logLevel, const char* format, va_list args)
{
  (void)logLevel;
  vfprintf(stdout, format, args);
  fputc('\n', stdout);
}

#if defined(_WIN32)
void CrashSignalHandler(int sig)
{
  (void)sig;
  std::abort();
}
#else
void CrashSignalHandler(int sig, siginfo_t* info, void* context)
{
  (void)info;
  (void)context;

  if (g_crashHandlerActive)
  {
    _exit(128 + sig);
  }
  g_crashHandlerActive = 1;

  const char msg[] = "FATAL: dmdutil-play-dump crashed. Stack trace:\n";
  ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
  (void)ignored;

  void* stack[64];
  int frameCount = backtrace(stack, 64);
  if (frameCount > 0)
  {
    backtrace_symbols_fd(stack, frameCount, STDERR_FILENO);
  }

  _exit(128 + sig);
}
#endif

void InstallCrashTraceHandlers()
{
#if defined(_WIN32)
  // Not implemented on Windows for this utility.
  return;
#else
  stack_t ss{};
  ss.ss_sp = g_crashAltStack;
  ss.ss_size = sizeof(g_crashAltStack);
  if (sigaltstack(&ss, nullptr) != 0)
  {
    return;
  }

  struct sigaction sa{};
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
  sa.sa_sigaction = CrashSignalHandler;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGFPE, &sa, nullptr);
#endif
}

enum class FrameFormat
{
  Indexed,
  RGB565,
  RGB888
};

enum class InputFormat
{
  Txt,
  Raw,
  Rgb565,
  Rgb888,
  Zip,
  Unknown
};

struct Frame
{
  uint32_t timestampMs = 0;
  uint32_t originalTimestampMs = 0;
  uint32_t durationMs = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  FrameFormat format = FrameFormat::Indexed;
  std::vector<uint8_t> data;
  std::vector<uint16_t> data16;
};

struct LiveJsonFrameRecord
{
  uint32_t index = 0;
  uint32_t sourceFrameIndex = 0;
  uint32_t originalFrameIndex = 0;
  uint32_t inputTimestampMs = 0;
  uint32_t inputDurationMs = 0;
  uint16_t inputWidth = 0;
  uint16_t inputHeight = 0;
  FrameFormat inputFormat = FrameFormat::Indexed;
  uint32_t inputCrc32 = 0;
  bool hasOutput = false;
  bool hasOutputTimestamp = false;
  uint32_t outputTimestampMs = 0;
  uint16_t outputWidth = 0;
  uint16_t outputHeight = 0;
  DMDUtil::DMD::Mode outputMode = DMDUtil::DMD::Mode::Unknown;
  uint64_t outputHashFNV1a64 = 0;
  uint32_t serumResult = 0xffffffff;
  uint32_t serumVersion = 0;
  uint32_t serumFrameId = 0xffffffff;
  uint32_t serumTriggerId = 0xffffffff;
  uint32_t serumRotationTimer = 0;
  uint32_t serumFeatureFlags = 0;
  uint32_t colorizeTimeUs = 0;
  uint32_t averageColorizeTimeUs = 0;
};

static bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
{
  if (suffix.size() > value.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i)
  {
    char a = value[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

static std::string GetBaseName(const std::string& path)
{
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

static std::string StripExtension(const std::string& name)
{
  size_t pos = name.find_last_of('.');
  if (pos == std::string::npos) return name;
  return name.substr(0, pos);
}

static bool IsZipFile(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return false;
  }
  unsigned char header[4] = {0};
  file.read(reinterpret_cast<char*>(header), sizeof(header));
  if (file.gcount() < (std::streamsize)sizeof(header))
  {
    return false;
  }
  if (header[0] != 'P' || header[1] != 'K') return false;
  if (header[2] == 0x03 && header[3] == 0x04) return true;
  if (header[2] == 0x05 && header[3] == 0x06) return true;
  if (header[2] == 0x07 && header[3] == 0x08) return true;
  return false;
}

static int HexToInt(char ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static uint8_t ScaleIndex(uint8_t value, uint8_t inDepth, uint8_t outDepth)
{
  if (inDepth == outDepth) return value;
  if (inDepth == 0 || outDepth == 0) return 0;

  const int maxIn = (1 << inDepth) - 1;
  const int maxOut = (1 << outDepth) - 1;
  if (maxIn <= 0) return 0;

  int scaled = (value * maxOut + (maxIn / 2)) / maxIn;
  if (scaled < 0) scaled = 0;
  if (scaled > maxOut) scaled = maxOut;
  return (uint8_t)scaled;
}

static uint8_t QuantizeToDepth(uint8_t r, uint8_t g, uint8_t b, uint8_t depth)
{
  int v = (int)(0.2126f * (float)r + 0.7152f * (float)g + 0.0722f * (float)b);
  if (v > 255) v = 255;
  return (depth == 2) ? (uint8_t)(v >> 6) : (uint8_t)(v >> 4);
}

static bool LoadTxtDumpStream(std::istream& file, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::string line;
  Frame current;
  bool inFrame = false;
  uint16_t width = 0;
  uint16_t height = 0;
  int maxValue = 0;

  auto finalizeFrame = [&]()
  {
    if (!inFrame) return;
    if (width == 0 || height == 0)
    {
      inFrame = false;
      return;
    }

    uint8_t inDepth = (maxValue <= 3) ? 2 : 4;
    if (maxValue > 15)
    {
      std::cerr << "Error: Invalid pixel value in txt dump\n";
      inFrame = false;
      return;
    }

    if (inDepth != outDepth)
    {
      for (size_t i = 0; i < current.data.size(); ++i)
      {
        current.data[i] = ScaleIndex(current.data[i], inDepth, outDepth);
      }
    }

    current.width = width;
    current.height = height;
    frames.push_back(current);
    current = Frame();
    inFrame = false;
  };

  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty())
    {
      finalizeFrame();
      width = 0;
      height = 0;
      maxValue = 0;
      continue;
    }

    if (!inFrame)
    {
      if (line.rfind("0x", 0) != 0 && line.rfind("0X", 0) != 0)
      {
        continue;
      }
      current = Frame();
      current.timestampMs = (uint32_t)strtoul(line.c_str(), nullptr, 16);
      current.originalTimestampMs = current.timestampMs;
      inFrame = true;
      width = 0;
      height = 0;
      maxValue = 0;
      continue;
    }

    if (width == 0)
    {
      width = (uint16_t)line.size();
    }
    else if (line.size() != width)
    {
      std::cerr << "Error: Inconsistent line width in txt dump\n";
      return false;
    }

    for (char ch : line)
    {
      int value = HexToInt(ch);
      if (value < 0)
      {
        std::cerr << "Error: Invalid hex digit in txt dump\n";
        return false;
      }
      current.data.push_back((uint8_t)value);
      if (value > maxValue) maxValue = value;
    }
    height++;
  }

  finalizeFrame();

  if (frames.empty())
  {
    std::cerr << "Error: No frames found in txt dump\n";
    return false;
  }

  return true;
}

static bool LoadTxtDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

  return LoadTxtDumpStream(file, outDepth, frames);
}

static bool ParseHexUint16(const std::string& line, size_t pos, uint16_t& value)
{
  if (pos + 4 > line.size()) return false;
  int h0 = HexToInt(line[pos]);
  int h1 = HexToInt(line[pos + 1]);
  int h2 = HexToInt(line[pos + 2]);
  int h3 = HexToInt(line[pos + 3]);
  if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return false;
  value = (uint16_t)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
  return true;
}

static bool ParseHexRgb24(const std::string& line, size_t pos, uint8_t& r, uint8_t& g, uint8_t& b)
{
  if (pos + 6 > line.size()) return false;
  int h0 = HexToInt(line[pos]);
  int h1 = HexToInt(line[pos + 1]);
  int h2 = HexToInt(line[pos + 2]);
  int h3 = HexToInt(line[pos + 3]);
  int h4 = HexToInt(line[pos + 4]);
  int h5 = HexToInt(line[pos + 5]);
  if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0) return false;
  r = (uint8_t)((h0 << 4) | h1);
  g = (uint8_t)((h2 << 4) | h3);
  b = (uint8_t)((h4 << 4) | h5);
  return true;
}

static bool LoadRgb565DumpStream(std::istream& file, uint8_t outDepth, std::vector<Frame>& frames,
                                 bool strictMode = true)
{
  (void)outDepth;
  std::string line;
  Frame current;
  bool inFrame = false;
  uint16_t width = 0;
  uint16_t height = 0;

  auto finalizeFrame = [&]()
  {
    if (!inFrame) return;
    if (width == 0 || height == 0)
    {
      inFrame = false;
      return;
    }

    current.width = width;
    current.height = height;
    current.format = FrameFormat::RGB565;
    frames.push_back(current);
    current = Frame();
    inFrame = false;
  };

  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty())
    {
      finalizeFrame();
      width = 0;
      height = 0;
      continue;
    }

    if (!inFrame)
    {
      if (line.rfind("0x", 0) != 0 && line.rfind("0X", 0) != 0)
      {
        continue;
      }
      current = Frame();
      current.timestampMs = (uint32_t)strtoul(line.c_str(), nullptr, 16);
      current.originalTimestampMs = current.timestampMs;
      inFrame = true;
      width = 0;
      height = 0;
      continue;
    }

    // Recover when a new timestamp header appears without a separating blank line.
    if (line.rfind("0x", 0) == 0 || line.rfind("0X", 0) == 0)
    {
      if (strictMode)
      {
        std::cerr << "Error: Missing blank separator in rgb565 dump\n";
        return false;
      }
      finalizeFrame();
      current = Frame();
      current.timestampMs = (uint32_t)strtoul(line.c_str(), nullptr, 16);
      current.originalTimestampMs = current.timestampMs;
      inFrame = true;
      width = 0;
      height = 0;
      continue;
    }

    if ((line.size() % 4) != 0)
    {
      if (strictMode)
      {
        std::cerr << "Error: Invalid line width in rgb565 dump\n";
        return false;
      }
      // Drop malformed frame and wait for next frame separator/header.
      current = Frame();
      inFrame = false;
      width = 0;
      height = 0;
      continue;
    }

    uint16_t lineWidth = (uint16_t)(line.size() / 4);
    if (width == 0)
    {
      width = lineWidth;
    }
    else if (lineWidth != width)
    {
      if (strictMode)
      {
        std::cerr << "Error: Inconsistent line width in rgb565 dump\n";
        return false;
      }
      current = Frame();
      inFrame = false;
      width = 0;
      height = 0;
      continue;
    }

    for (size_t pos = 0; pos < line.size(); pos += 4)
    {
      uint16_t value = 0;
      if (!ParseHexUint16(line, pos, value))
      {
        if (strictMode)
        {
          std::cerr << "Error: Invalid hex digit in rgb565 dump\n";
          return false;
        }
        current = Frame();
        inFrame = false;
        width = 0;
        height = 0;
        break;
      }
      current.data16.push_back(value);
    }
    if (!inFrame)
    {
      continue;
    }
    height++;
  }

  finalizeFrame();

  if (frames.empty())
  {
    std::cerr << "Error: No frames found in rgb565 dump\n";
    return false;
  }

  return true;
}

static bool LoadRgb565Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

  return LoadRgb565DumpStream(file, outDepth, frames);
}

static bool LoadRgb888DumpStream(std::istream& file, uint8_t outDepth, std::vector<Frame>& frames)
{
  (void)outDepth;
  std::string line;
  Frame current;
  bool inFrame = false;
  uint16_t width = 0;
  uint16_t height = 0;

  auto finalizeFrame = [&]()
  {
    if (!inFrame) return;
    if (width == 0 || height == 0)
    {
      inFrame = false;
      return;
    }

    current.width = width;
    current.height = height;
    current.format = FrameFormat::RGB888;
    frames.push_back(current);
    current = Frame();
    inFrame = false;
  };

  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty())
    {
      finalizeFrame();
      width = 0;
      height = 0;
      continue;
    }

    if (!inFrame)
    {
      if (line.rfind("0x", 0) != 0 && line.rfind("0X", 0) != 0)
      {
        continue;
      }
      current = Frame();
      current.timestampMs = (uint32_t)strtoul(line.c_str(), nullptr, 16);
      current.originalTimestampMs = current.timestampMs;
      inFrame = true;
      width = 0;
      height = 0;
      continue;
    }

    if ((line.size() % 6) != 0)
    {
      std::cerr << "Error: Invalid line width in rgb888 dump\n";
      return false;
    }

    uint16_t lineWidth = (uint16_t)(line.size() / 6);
    if (width == 0)
    {
      width = lineWidth;
    }
    else if (lineWidth != width)
    {
      std::cerr << "Error: Inconsistent line width in rgb888 dump\n";
      return false;
    }

    for (size_t pos = 0; pos < line.size(); pos += 6)
    {
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      if (!ParseHexRgb24(line, pos, r, g, b))
      {
        std::cerr << "Error: Invalid hex digit in rgb888 dump\n";
        return false;
      }
      current.data.push_back(r);
      current.data.push_back(g);
      current.data.push_back(b);
    }
    height++;
  }

  finalizeFrame();

  if (frames.empty())
  {
    std::cerr << "Error: No frames found in rgb888 dump\n";
    return false;
  }

  return true;
}

static bool LoadRgb888Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

  return LoadRgb888DumpStream(file, outDepth, frames);
}

static void FinalizeFrameDurations(std::vector<Frame>& frames)
{
  if (frames.empty()) return;

  bool monotonic = true;
  for (size_t i = 1; i < frames.size(); ++i)
  {
    if (frames[i].timestampMs < frames[i - 1].timestampMs)
    {
      monotonic = false;
      break;
    }
  }

  if (!monotonic)
  {
    uint32_t accumulated = 0;
    for (Frame& frame : frames)
    {
      frame.durationMs = frame.timestampMs;
      frame.originalTimestampMs = accumulated;
      accumulated += frame.durationMs;
    }
    return;
  }

  for (size_t i = 0; i + 1 < frames.size(); ++i)
  {
    uint32_t curr = frames[i].timestampMs;
    uint32_t next = frames[i + 1].timestampMs;
    frames[i].durationMs = (next > curr) ? (next - curr) : 0;
    frames[i].originalTimestampMs = curr;
  }

  if (frames.size() > 1)
  {
    frames.back().durationMs = frames[frames.size() - 2].durationMs;
  }
  frames.back().originalTimestampMs = frames.back().timestampMs;
}

static bool ConvertUpdateToIndexed(const DMDUtil::DMD::Update& update, uint8_t outDepth, Frame& frame)
{
  using Mode = DMDUtil::DMD::Mode;
  frame.width = update.width;
  frame.height = update.height;
  frame.format = FrameFormat::Indexed;
  frame.data16.clear();
  if (frame.width == 0 || frame.height == 0) return false;

  const int length = (int)frame.width * frame.height;
  frame.data.assign(length, 0);

  switch (update.mode)
  {
    case Mode::Data:
    case Mode::NotColorized:
    {
      if (!update.hasData) return false;
      uint8_t inDepth = (uint8_t)update.depth;
      if (inDepth == 0 || inDepth > 8) return false;
      for (int i = 0; i < length; ++i)
      {
        frame.data[i] = ScaleIndex(update.data[i], inDepth, outDepth);
      }
      return true;
    }
    case Mode::RGB24:
    {
      if (!update.hasData) return false;
      for (int i = 0; i < length; ++i)
      {
        int pos = i * 3;
        uint8_t r = update.data[pos];
        uint8_t g = update.data[pos + 1];
        uint8_t b = update.data[pos + 2];
        frame.data[i] = QuantizeToDepth(r, g, b, outDepth);
      }
      return true;
    }
    case Mode::RGB16:
    case Mode::SerumV2_32:
    case Mode::SerumV2_32_64:
    case Mode::SerumV2_64:
    case Mode::SerumV2_64_32:
    {
      const uint16_t* src = update.segData;
      for (int i = 0; i < length; ++i)
      {
        uint16_t value = src[i];
        uint8_t r = (uint8_t)((value >> 11) & 0x1F);
        uint8_t g = (uint8_t)((value >> 5) & 0x3F);
        uint8_t b = (uint8_t)(value & 0x1F);
        r = (uint8_t)((r << 3) | (r >> 2));
        g = (uint8_t)((g << 2) | (g >> 4));
        b = (uint8_t)((b << 3) | (b >> 2));
        frame.data[i] = QuantizeToDepth(r, g, b, outDepth);
      }
      return true;
    }
    case Mode::SerumV1:
    case Mode::Vni:
    {
      if (!update.hasData) return false;
      uint8_t inDepth = (uint8_t)update.depth;
      if (inDepth == 0 || inDepth > 8) return false;
      int colors = 1 << inDepth;
      const uint8_t* palette = reinterpret_cast<const uint8_t*>(update.segData);
      for (int i = 0; i < length; ++i)
      {
        uint8_t idx = update.data[i];
        if (idx >= colors) idx = (uint8_t)(colors - 1);
        int pos = idx * 3;
        uint8_t r = palette[pos];
        uint8_t g = palette[pos + 1];
        uint8_t b = palette[pos + 2];
        frame.data[i] = QuantizeToDepth(r, g, b, outDepth);
      }
      return true;
    }
    default:
      return false;
  }
}

static bool LoadRawDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

  while (true)
  {
    uint32_t timestamp = 0;
    uint32_t size = 0;
    if (!file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp))) break;
    if (!file.read(reinterpret_cast<char*>(&size), sizeof(size))) break;

    if (size < sizeof(DMDUtil::DMD::Update))
    {
      std::cerr << "Error: Raw dump frame size is too small\n";
      return false;
    }

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) break;

    DMDUtil::DMD::Update update;
    memcpy(&update, buffer.data(), sizeof(update));

    Frame frame;
    frame.timestampMs = timestamp;
    frame.originalTimestampMs = timestamp;
    if (ConvertUpdateToIndexed(update, outDepth, frame))
    {
      frames.push_back(std::move(frame));
    }
  }

  if (frames.empty())
  {
    std::cerr << "Error: No frames found in raw dump\n";
    return false;
  }

  return true;
}

static bool LoadRawDumpFromBuffer(const uint8_t* data, size_t size, uint8_t outDepth, std::vector<Frame>& frames)
{
  size_t offset = 0;
  while (offset + sizeof(uint32_t) * 2 <= size)
  {
    uint32_t timestamp = 0;
    uint32_t frameSize = 0;
    memcpy(&timestamp, data + offset, sizeof(timestamp));
    offset += sizeof(timestamp);
    memcpy(&frameSize, data + offset, sizeof(frameSize));
    offset += sizeof(frameSize);

    if (frameSize < sizeof(DMDUtil::DMD::Update))
    {
      std::cerr << "Error: Raw dump frame size is too small\n";
      return false;
    }

    if (offset + frameSize > size)
    {
      break;
    }

    std::vector<uint8_t> buffer(frameSize);
    memcpy(buffer.data(), data + offset, frameSize);
    offset += frameSize;

    DMDUtil::DMD::Update update;
    memcpy(&update, buffer.data(), sizeof(update));

    Frame frame;
    frame.timestampMs = timestamp;
    frame.originalTimestampMs = timestamp;
    if (ConvertUpdateToIndexed(update, outDepth, frame))
    {
      frames.push_back(std::move(frame));
    }
  }

  if (frames.empty())
  {
    std::cerr << "Error: No frames found in raw dump\n";
    return false;
  }

  return true;
}

static InputFormat DetectFormatFromName(const std::string& name)
{
  if (EndsWithCaseInsensitive(name, ".565.txt")) return InputFormat::Rgb565;
  if (EndsWithCaseInsensitive(name, ".888.txt")) return InputFormat::Rgb888;
  if (EndsWithCaseInsensitive(name, ".raw")) return InputFormat::Raw;
  if (EndsWithCaseInsensitive(name, ".txt")) return InputFormat::Txt;
  return InputFormat::Unknown;
}

static bool LoadZipDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_file(&zip, path.c_str(), 0))
  {
    std::cerr << "Error: Unable to open zip file: " << path << "\n";
    return false;
  }

  int bestIndex = -1;
  InputFormat bestFormat = InputFormat::Unknown;
  int bestPriority = -1;
  int firstFileIndex = -1;
  const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);

  for (mz_uint i = 0; i < fileCount; ++i)
  {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, i, &stat))
    {
      continue;
    }
    if (stat.m_is_directory)
    {
      continue;
    }
    if (firstFileIndex < 0)
    {
      firstFileIndex = (int)i;
    }

    InputFormat format = DetectFormatFromName(stat.m_filename);
    int priority = -1;
    switch (format)
    {
      case InputFormat::Rgb565:
        priority = 3;
        break;
      case InputFormat::Rgb888:
        priority = 2;
        break;
      case InputFormat::Txt:
        priority = 1;
        break;
      case InputFormat::Raw:
        priority = 0;
        break;
      default:
        break;
    }

    if (priority > bestPriority)
    {
      bestPriority = priority;
      bestIndex = (int)i;
      bestFormat = format;
    }
  }

  if (bestIndex < 0 && firstFileIndex >= 0)
  {
    bestIndex = firstFileIndex;
    bestFormat = InputFormat::Unknown;
  }

  if (bestIndex < 0)
  {
    mz_zip_reader_end(&zip);
    std::cerr << "Error: No files found in zip dump\n";
    return false;
  }

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip, (mz_uint)bestIndex, &stat))
  {
    mz_zip_reader_end(&zip);
    std::cerr << "Error: Unable to read zip entry\n";
    return false;
  }

  if (stat.m_uncomp_size > static_cast<mz_uint64>(std::numeric_limits<size_t>::max()))
  {
    mz_zip_reader_end(&zip);
    std::cerr << "Error: Zip entry too large to extract\n";
    return false;
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(stat.m_uncomp_size));
  if (!mz_zip_reader_extract_to_mem(&zip, (mz_uint)bestIndex, buffer.data(), buffer.size(), 0))
  {
    mz_zip_reader_end(&zip);
    std::cerr << "Error: Unable to extract zip entry\n";
    return false;
  }

  if (bestFormat == InputFormat::Unknown)
  {
    bestFormat = DetectFormatFromName(stat.m_filename);
  }

  bool ok = false;
  if (bestFormat == InputFormat::Rgb565)
  {
    std::string content(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    std::istringstream stream(content);
    ok = LoadRgb565DumpStream(stream, outDepth, frames);
  }
  else if (bestFormat == InputFormat::Rgb888)
  {
    std::string content(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    std::istringstream stream(content);
    ok = LoadRgb888DumpStream(stream, outDepth, frames);
  }
  else if (bestFormat == InputFormat::Raw)
  {
    ok = LoadRawDumpFromBuffer(buffer.data(), buffer.size(), outDepth, frames);
  }
  else
  {
    std::string content(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    std::istringstream stream(content);
    ok = LoadTxtDumpStream(stream, outDepth, frames);
  }

  mz_zip_reader_end(&zip);
  return ok;
}

static bool ParseServer(const std::string& value, std::string& host, int& port)
{
  if (value.empty()) return false;
  size_t colon = value.find_last_of(':');
  if (colon == std::string::npos)
  {
    host = value;
    port = 6789;
    return true;
  }

  host = value.substr(0, colon);
  std::string portStr = value.substr(colon + 1);
  if (portStr.empty())
  {
    port = 6789;
    return true;
  }

  port = atoi(portStr.c_str());
  if (port <= 0) port = 6789;
  return true;
}

static bool SetEnvFlag(const char* name, const char* value)
{
#if defined(_WIN32)
  return _putenv_s(name, value) == 0;
#else
  return setenv(name, value, 1) == 0;
#endif
}

static uint64_t GetProcessRssBytes()
{
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
  {
    return static_cast<uint64_t>(pmc.WorkingSetSize);
  }
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
  {
    return static_cast<uint64_t>(info.resident_size);
  }
  return 0;
#elif defined(__linux__)
  long rssPages = 0;
  FILE* fp = fopen("/proc/self/statm", "r");
  if (!fp)
  {
    return 0;
  }
  long totalPages = 0;
  if (fscanf(fp, "%ld %ld", &totalPages, &rssPages) != 2)
  {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  long pageSize = sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) return 0;
  return static_cast<uint64_t>(rssPages) * static_cast<uint64_t>(pageSize);
#else
  return 0;
#endif
}

static uint64_t HashFrameRgb565(const std::vector<uint16_t>& data16)
{
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data16.data());
  const size_t byteCount = data16.size() * sizeof(uint16_t);
  uint64_t hash = 1469598103934665603ull;  // FNV-1a
  for (size_t i = 0; i < byteCount; ++i)
  {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  hash ^= byteCount;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t HashBytesFNV1a64(const uint8_t* bytes, size_t byteCount)
{
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < byteCount; ++i)
  {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  hash ^= byteCount;
  hash *= 1099511628211ull;
  return hash;
}

static uint32_t ComputeInputCrc32(const Frame& frame)
{
  if (frame.format == FrameFormat::RGB565)
  {
    const auto* bytes = reinterpret_cast<const unsigned char*>(frame.data16.data());
    return mz_crc32(0, bytes, static_cast<mz_ulong>(frame.data16.size() * sizeof(uint16_t)));
  }
  return mz_crc32(0, reinterpret_cast<const unsigned char*>(frame.data.data()),
                  static_cast<mz_ulong>(frame.data.size()));
}

static const char* FrameFormatToString(FrameFormat format)
{
  switch (format)
  {
    case FrameFormat::Indexed:
      return "indexed";
    case FrameFormat::RGB565:
      return "rgb565";
    case FrameFormat::RGB888:
      return "rgb888";
  }
  return "unknown";
}

static const char* OutputModeToString(DMDUtil::DMD::Mode mode)
{
  switch (mode)
  {
    case DMDUtil::DMD::Mode::Data:
      return "data";
    case DMDUtil::DMD::Mode::RGB24:
      return "rgb24";
    case DMDUtil::DMD::Mode::RGB16:
      return "rgb16";
    case DMDUtil::DMD::Mode::AlphaNumeric:
      return "alphanumeric";
    case DMDUtil::DMD::Mode::SerumV1:
      return "serum-v1";
    case DMDUtil::DMD::Mode::SerumV2_32:
      return "serum-v2-32";
    case DMDUtil::DMD::Mode::SerumV2_32_64:
      return "serum-v2-32-64";
    case DMDUtil::DMD::Mode::SerumV2_64:
      return "serum-v2-64";
    case DMDUtil::DMD::Mode::SerumV2_64_32:
      return "serum-v2-64-32";
    case DMDUtil::DMD::Mode::NotColorized:
      return "not-colorized";
    case DMDUtil::DMD::Mode::Vni:
      return "vni";
    case DMDUtil::DMD::Mode::SerumCommand:
      return "serum-command";
    default:
      return "unknown";
  }
}

static std::string EscapeJsonString(const std::string& input)
{
  std::ostringstream oss;
  for (char ch : input)
  {
    switch (ch)
    {
      case '\\':
        oss << "\\\\";
        break;
      case '"':
        oss << "\\\"";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << ch;
        break;
    }
  }
  return oss.str();
}

static void WriteFeatureFlagNames(std::ostream& out, uint32_t featureFlags)
{
  bool first = true;
  auto emit = [&](const char* name, uint32_t bit)
  {
    if ((featureFlags & bit) == 0)
    {
      return;
    }
    if (!first)
    {
      out << ", ";
    }
    out << '"' << name << '"';
    first = false;
  };

  emit("matched", SERUM_RUNTIME_FEATURE_MATCHED);
  emit("background", SERUM_RUNTIME_FEATURE_BACKGROUND);
  emit("dynamic", SERUM_RUNTIME_FEATURE_DYNAMIC);
  emit("dynamicExtra", SERUM_RUNTIME_FEATURE_DYNAMIC_EXTRA);
  emit("sprites", SERUM_RUNTIME_FEATURE_SPRITES);
  emit("shapeSprites", SERUM_RUNTIME_FEATURE_SHAPE_SPRITES);
  emit("colorRotation", SERUM_RUNTIME_FEATURE_COLOR_ROTATION);
  emit("scene", SERUM_RUNTIME_FEATURE_SCENE);
  emit("trigger", SERUM_RUNTIME_FEATURE_TRIGGER);
  emit("monochromeFallback", SERUM_RUNTIME_FEATURE_MONOCHROME_FALLBACK);
}

static LiveJsonFrameRecord MakeLiveJsonFrameRecord(uint32_t index, const Frame& frame,
                                                   const DMDUtil::DMD::SerumCapture& capture)
{
  LiveJsonFrameRecord record;
  record.index = index;
  record.sourceFrameIndex = capture.sourceFrameIndex;
  record.originalFrameIndex = capture.originalFrameIndex;
  record.inputTimestampMs = capture.inputTimestampMs;
  record.inputDurationMs = capture.inputDurationMs;
  record.inputWidth = frame.width;
  record.inputHeight = frame.height;
  record.inputFormat = frame.format;
  record.inputCrc32 = capture.inputCrc32;
  record.hasOutput = capture.hasOutput;
  record.hasOutputTimestamp = capture.hasTimestamp;
  record.outputTimestampMs = capture.outputTimestampMs;
  record.outputWidth = capture.update.width;
  record.outputHeight = capture.update.height;
  record.outputMode = capture.update.mode;
  record.serumResult = capture.serumResult;
  record.serumVersion = capture.serumVersion;
  record.serumFrameId = capture.serumFrameId;
  record.serumTriggerId = capture.serumTriggerId;
  record.serumRotationTimer = capture.serumRotationTimer;
  record.serumFeatureFlags = capture.serumFeatureFlags;
  record.colorizeTimeUs = capture.colorizeTimeUs;
  record.averageColorizeTimeUs = capture.averageColorizeTimeUs;

  if (capture.hasOutput)
  {
    if (capture.update.mode == DMDUtil::DMD::Mode::RGB24)
    {
      record.outputHashFNV1a64 =
          HashBytesFNV1a64(capture.update.data, static_cast<size_t>(capture.update.width) * capture.update.height * 3u);
    }
    else if (capture.update.mode == DMDUtil::DMD::Mode::SerumV1 || capture.update.mode == DMDUtil::DMD::Mode::Data ||
             capture.update.mode == DMDUtil::DMD::Mode::NotColorized || capture.update.mode == DMDUtil::DMD::Mode::Vni)
    {
      record.outputHashFNV1a64 =
          HashBytesFNV1a64(capture.update.data, static_cast<size_t>(capture.update.width) * capture.update.height);
    }
    else
    {
      record.outputHashFNV1a64 =
          HashBytesFNV1a64(reinterpret_cast<const uint8_t*>(capture.update.segData),
                           static_cast<size_t>(capture.update.width) * capture.update.height * sizeof(uint16_t));
    }
  }

  return record;
}

static uint64_t HashFrameInputSignature(const Frame& frame)
{
  uint64_t hash = 1469598103934665603ull;
  auto mixByte = [&](uint8_t value)
  {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  auto mixU16 = [&](uint16_t value)
  {
    mixByte(static_cast<uint8_t>(value & 0xff));
    mixByte(static_cast<uint8_t>((value >> 8) & 0xff));
  };
  auto mixU32 = [&](uint32_t value)
  {
    mixByte(static_cast<uint8_t>(value & 0xff));
    mixByte(static_cast<uint8_t>((value >> 8) & 0xff));
    mixByte(static_cast<uint8_t>((value >> 16) & 0xff));
    mixByte(static_cast<uint8_t>((value >> 24) & 0xff));
  };

  mixU16(frame.width);
  mixU16(frame.height);
  mixByte(static_cast<uint8_t>(frame.format));
  if (frame.format == FrameFormat::RGB565)
  {
    for (uint16_t value : frame.data16)
    {
      mixU16(value);
    }
  }
  else
  {
    for (uint8_t value : frame.data)
    {
      mixByte(value);
    }
  }
  mixU32(static_cast<uint32_t>(frame.data.size()));
  mixU32(static_cast<uint32_t>(frame.data16.size()));
  return hash;
}

static const char* DumpFileExtensionForFormat(FrameFormat format)
{
  switch (format)
  {
    case FrameFormat::Indexed:
      return ".txt";
    case FrameFormat::RGB565:
      return ".565.txt";
    case FrameFormat::RGB888:
      return ".888.txt";
  }
  return ".txt";
}

static bool FramesShareFormat(const std::vector<Frame>& frames, FrameFormat& format)
{
  if (frames.empty())
  {
    format = FrameFormat::Indexed;
    return true;
  }

  format = frames.front().format;
  for (const Frame& frame : frames)
  {
    if (frame.format != format)
    {
      return false;
    }
  }
  return true;
}

static std::string BuildCoverageDumpPath(const std::string& coverageJsonPath, FrameFormat format)
{
  std::filesystem::path path(coverageJsonPath);
  path.replace_extension("");
  path += DumpFileExtensionForFormat(format);
  return path.string();
}

static void RebaseFrameTiming(std::vector<Frame>& frames, uint32_t stepMs)
{
  if (stepMs == 0)
  {
    stepMs = 1;
  }
  uint32_t timestamp = 0;
  for (Frame& frame : frames)
  {
    frame.timestampMs = timestamp;
    frame.originalTimestampMs = timestamp;
    frame.durationMs = stepMs;
    timestamp += stepMs;
  }
}

static void AppendCoverageWindow(size_t index, size_t frameCount, uint32_t transitionTail,
                                 std::vector<uint32_t>& selectedIndices)
{
  for (uint32_t offset = 0; offset <= transitionTail && index + offset < frameCount; ++offset)
  {
    selectedIndices.push_back(static_cast<uint32_t>(index + offset));
  }
}

static bool HasInterestingSerumFeature(uint32_t featureFlags)
{
  constexpr uint32_t kInterestingMask = SERUM_RUNTIME_FEATURE_BACKGROUND | SERUM_RUNTIME_FEATURE_DYNAMIC |
                                        SERUM_RUNTIME_FEATURE_DYNAMIC_EXTRA | SERUM_RUNTIME_FEATURE_SPRITES |
                                        SERUM_RUNTIME_FEATURE_SHAPE_SPRITES | SERUM_RUNTIME_FEATURE_COLOR_ROTATION |
                                        SERUM_RUNTIME_FEATURE_SCENE;
  return (featureFlags & kInterestingMask) != 0;
}

static void TrimSelectedIndicesEvenly(std::vector<uint32_t>& selectedIndices, uint32_t maxFrames)
{
  if (maxFrames == 0 || selectedIndices.size() <= maxFrames)
  {
    return;
  }
  if (maxFrames == 1)
  {
    selectedIndices.resize(1);
    return;
  }

  std::vector<uint32_t> trimmed;
  trimmed.reserve(maxFrames);
  const size_t lastIndex = selectedIndices.size() - 1;
  for (uint32_t i = 0; i < maxFrames; ++i)
  {
    const size_t pick = (static_cast<size_t>(i) * lastIndex) / (maxFrames - 1);
    trimmed.push_back(selectedIndices[pick]);
  }
  std::sort(trimmed.begin(), trimmed.end());
  trimmed.erase(std::unique(trimmed.begin(), trimmed.end()), trimmed.end());
  for (uint32_t candidate : selectedIndices)
  {
    if (trimmed.size() >= maxFrames)
    {
      break;
    }
    if (std::find(trimmed.begin(), trimmed.end(), candidate) == trimmed.end())
    {
      trimmed.push_back(candidate);
    }
  }
  std::sort(trimmed.begin(), trimmed.end());
  selectedIndices.swap(trimmed);
}

static std::vector<uint32_t> BuildCoverageSelectedIndices(
    const std::vector<Frame>& frames, const std::vector<uint64_t>& signatures, uint32_t transitionTail,
    uint32_t maxFrames, const std::vector<LiveJsonFrameRecord>* liveFrames, bool* usedRuntimeMetadata = nullptr,
    std::vector<uint64_t>* sortedUniqueSignaturesOut = nullptr,
    std::vector<uint32_t>* sortedUniqueFeatureMasksOut = nullptr,
    std::vector<uint32_t>* sortedFeatureSummaryMasksOut = nullptr,
    std::unordered_map<uint32_t, uint32_t>* featureMaskTotalCountsOut = nullptr,
    std::unordered_map<uint32_t, uint32_t>* featureMaskSelectedCountsOut = nullptr)
{
  std::unordered_set<uint64_t> uniqueSignatures;
  std::unordered_set<uint32_t> uniqueFeatureMasks;
  std::unordered_map<uint32_t, uint32_t> featureMaskSamples;
  std::unordered_map<uint64_t, uint32_t> featureCrcSamples;
  std::unordered_map<uint32_t, uint32_t> featureMaskTotalCounts;
  std::unordered_map<uint32_t, uint32_t> featureMaskSelectedCounts;
  std::vector<uint32_t> selectedIndices;
  uint64_t previousSignature = 0;
  uint32_t previousFeatureFlags = 0;
  bool havePreviousSignature = false;
  bool havePreviousFeatureFlags = false;
  bool usedRuntimeMetadataLocal = false;
  constexpr uint32_t kPerFeatureMaskBudget = 6;
  constexpr uint32_t kPerFeatureCrcBudget = 3;

  for (size_t i = 0; i < signatures.size() && i < frames.size(); ++i)
  {
    const uint64_t signature = signatures[i];
    bool selectCurrent = uniqueSignatures.insert(signature).second;
    if (!selectCurrent && havePreviousSignature && signature != previousSignature)
    {
      selectCurrent = true;
    }

    if (liveFrames != nullptr && i < liveFrames->size())
    {
      const LiveJsonFrameRecord& liveFrame = (*liveFrames)[i];
      const uint32_t featureFlags = liveFrame.serumFeatureFlags;
      const bool interestingFeature = HasInterestingSerumFeature(featureFlags);
      ++featureMaskTotalCounts[featureFlags];
      if (liveFrame.serumResult != 0 || featureFlags != 0 || liveFrame.hasOutput)
      {
        usedRuntimeMetadataLocal = true;
      }
      if (uniqueFeatureMasks.insert(featureFlags).second)
      {
        selectCurrent = true;
      }
      if (!selectCurrent && havePreviousFeatureFlags && featureFlags != previousFeatureFlags &&
          (interestingFeature || HasInterestingSerumFeature(previousFeatureFlags)))
      {
        selectCurrent = true;
      }
      if (!selectCurrent && interestingFeature)
      {
        uint32_t& maskBudget = featureMaskSamples[featureFlags];
        if (maskBudget < kPerFeatureMaskBudget)
        {
          ++maskBudget;
          selectCurrent = true;
        }
      }
      if (!selectCurrent && interestingFeature)
      {
        const uint64_t featureCrcKey = (static_cast<uint64_t>(featureFlags) << 32) | liveFrame.inputCrc32;
        uint32_t& crcBudget = featureCrcSamples[featureCrcKey];
        if (crcBudget < kPerFeatureCrcBudget)
        {
          ++crcBudget;
          selectCurrent = true;
        }
      }
      previousFeatureFlags = featureFlags;
      havePreviousFeatureFlags = true;
    }

    if (selectCurrent)
    {
      AppendCoverageWindow(i, frames.size(), transitionTail, selectedIndices);
    }
    previousSignature = signature;
    havePreviousSignature = true;
  }

  std::sort(selectedIndices.begin(), selectedIndices.end());
  selectedIndices.erase(std::unique(selectedIndices.begin(), selectedIndices.end()), selectedIndices.end());
  TrimSelectedIndicesEvenly(selectedIndices, maxFrames);

  if (liveFrames != nullptr)
  {
    for (uint32_t selectedIndex : selectedIndices)
    {
      if (selectedIndex < liveFrames->size())
      {
        ++featureMaskSelectedCounts[(*liveFrames)[selectedIndex].serumFeatureFlags];
      }
    }
  }

  if (usedRuntimeMetadata != nullptr)
  {
    *usedRuntimeMetadata = usedRuntimeMetadataLocal;
  }
  if (sortedUniqueSignaturesOut != nullptr)
  {
    sortedUniqueSignaturesOut->assign(uniqueSignatures.begin(), uniqueSignatures.end());
    std::sort(sortedUniqueSignaturesOut->begin(), sortedUniqueSignaturesOut->end());
  }
  if (sortedUniqueFeatureMasksOut != nullptr)
  {
    sortedUniqueFeatureMasksOut->assign(uniqueFeatureMasks.begin(), uniqueFeatureMasks.end());
    std::sort(sortedUniqueFeatureMasksOut->begin(), sortedUniqueFeatureMasksOut->end());
  }
  if (sortedFeatureSummaryMasksOut != nullptr)
  {
    sortedFeatureSummaryMasksOut->clear();
    sortedFeatureSummaryMasksOut->reserve(featureMaskTotalCounts.size());
    for (const auto& entry : featureMaskTotalCounts)
    {
      sortedFeatureSummaryMasksOut->push_back(entry.first);
    }
    std::sort(sortedFeatureSummaryMasksOut->begin(), sortedFeatureSummaryMasksOut->end());
  }
  if (featureMaskTotalCountsOut != nullptr)
  {
    *featureMaskTotalCountsOut = std::move(featureMaskTotalCounts);
  }
  if (featureMaskSelectedCountsOut != nullptr)
  {
    *featureMaskSelectedCountsOut = std::move(featureMaskSelectedCounts);
  }

  return selectedIndices;
}

static bool WriteCoverageJson(const std::string& outputPath, const std::string& inputPath,
                              const std::vector<Frame>& frames, const std::vector<uint64_t>& signatures,
                              uint32_t transitionTail, uint32_t maxFrames,
                              const std::vector<LiveJsonFrameRecord>* liveFrames = nullptr,
                              const std::string* filteredDumpPath = nullptr)
{
  bool usedRuntimeMetadata = false;
  std::vector<uint64_t> sortedSignatures;
  std::vector<uint32_t> sortedFeatureMasks;
  std::vector<uint32_t> sortedFeatureSummaryMasks;
  std::unordered_map<uint32_t, uint32_t> featureMaskTotalCounts;
  std::unordered_map<uint32_t, uint32_t> featureMaskSelectedCounts;
  const std::vector<uint32_t> selectedIndices = BuildCoverageSelectedIndices(
      frames, signatures, transitionTail, maxFrames, liveFrames, &usedRuntimeMetadata, &sortedSignatures,
      &sortedFeatureMasks, &sortedFeatureSummaryMasks, &featureMaskTotalCounts, &featureMaskSelectedCounts);

  std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }
  out << "{\n";
  out << "  \"schema\": \"dmdutil.coverage.v3\",\n";
  out << "  \"input\": \"" << EscapeJsonString(inputPath) << "\",\n";
  if (filteredDumpPath != nullptr && !filteredDumpPath->empty())
  {
    out << "  \"filteredDump\": \"" << EscapeJsonString(*filteredDumpPath) << "\",\n";
  }
  out << "  \"frameCount\": " << frames.size() << ",\n";
  out << "  \"selectedFrameCount\": " << selectedIndices.size() << ",\n";
  out << "  \"usedRuntimeMetadata\": " << (usedRuntimeMetadata ? "true" : "false") << ",\n";
  out << "  \"selectedIndices\": [";
  for (size_t i = 0; i < selectedIndices.size(); ++i)
  {
    if (i > 0) out << ", ";
    out << selectedIndices[i];
  }
  out << "],\n";
  out << "  \"signatures\": [";
  for (size_t i = 0; i < sortedSignatures.size(); ++i)
  {
    if (i > 0) out << ", ";
    out << sortedSignatures[i];
  }
  out << "]";
  if (usedRuntimeMetadata)
  {
    out << ",\n  \"featureMasks\": [";
    for (size_t i = 0; i < sortedFeatureMasks.size(); ++i)
    {
      if (i > 0) out << ", ";
      out << sortedFeatureMasks[i];
    }
    out << "],\n";
    out << "  \"featureSummary\": [\n";
    for (size_t i = 0; i < sortedFeatureSummaryMasks.size(); ++i)
    {
      const uint32_t featureMask = sortedFeatureSummaryMasks[i];
      out << "    {\"mask\": " << featureMask << ", \"names\": [";
      WriteFeatureFlagNames(out, featureMask);
      out << "]"
          << ", \"totalCount\": " << featureMaskTotalCounts[featureMask]
          << ", \"selectedCount\": " << featureMaskSelectedCounts[featureMask] << "}";
      if (i + 1 < sortedFeatureSummaryMasks.size())
      {
        out << ",";
      }
      out << "\n";
    }
    out << "  ]\n";
  }
  else
  {
    out << "\n";
  }
  out << "}\n";
  return true;
}

static bool WriteFilteredTextDump(const std::string& outputPath, const std::vector<Frame>& frames)
{
  FrameFormat format = FrameFormat::Indexed;
  if (!FramesShareFormat(frames, format))
  {
    std::cerr << "Error: Coverage export requires a consistent frame format to write a portable text dump\n";
    return false;
  }

  std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }

  for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
  {
    const Frame& frame = frames[frameIndex];
    out << "0x" << std::hex << std::nouppercase << frame.timestampMs << std::dec << "\n";

    if (format == FrameFormat::Indexed)
    {
      size_t offset = 0;
      for (uint16_t y = 0; y < frame.height; ++y)
      {
        for (uint16_t x = 0; x < frame.width; ++x)
        {
          const uint8_t value = frame.data[offset++];
          out << "0123456789abcdef"[value & 0x0f];
        }
        out << "\n";
      }
    }
    else if (format == FrameFormat::RGB565)
    {
      size_t offset = 0;
      for (uint16_t y = 0; y < frame.height; ++y)
      {
        for (uint16_t x = 0; x < frame.width; ++x)
        {
          out << std::hex << std::setw(4) << std::setfill('0') << frame.data16[offset++] << std::dec;
        }
        out << "\n";
      }
    }
    else
    {
      size_t offset = 0;
      for (uint16_t y = 0; y < frame.height; ++y)
      {
        for (uint16_t x = 0; x < frame.width; ++x)
        {
          out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(frame.data[offset++])
              << std::setw(2) << std::setfill('0') << static_cast<unsigned>(frame.data[offset++]) << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(frame.data[offset++]) << std::dec;
        }
        out << "\n";
      }
    }

    if (frameIndex + 1 < frames.size())
    {
      out << "\n";
    }
  }

  return true;
}

static uint64_t ComputePlannedSleepMsForFrame(const Frame& frame, bool delaySet, uint32_t delayMs)
{
  if (delaySet)
  {
    uint32_t sleepMs = delayMs;
    if (frame.durationMs > 0 && frame.durationMs < sleepMs)
    {
      sleepMs = frame.durationMs;
    }
    return sleepMs;
  }
  return frame.durationMs;
}

static uint64_t ComputeEstimatedPlaybackMs(const std::vector<Frame>& frames, bool delaySet, uint32_t delayMs)
{
  uint64_t totalMs = 0;
  for (const Frame& frame : frames)
  {
    totalMs += ComputePlannedSleepMsForFrame(frame, delaySet, delayMs);
  }
  return totalMs;
}

static std::string FormatDurationMs(uint64_t durationMs)
{
  const uint64_t totalSeconds = durationMs / 1000;
  const uint64_t hours = totalSeconds / 3600;
  const uint64_t minutes = (totalSeconds % 3600) / 60;
  const uint64_t seconds = totalSeconds % 60;
  const uint64_t millis = durationMs % 1000;

  std::ostringstream oss;
  if (hours > 0)
  {
    oss << hours << "h ";
  }
  oss << minutes << "m " << seconds << "s " << millis << "ms";
  return oss.str();
}

static bool WaitForDumpersToSettle(DMDUtil::DMD& dmd, std::atomic<bool>& stopRequested)
{
  constexpr auto kQuietWindow = std::chrono::milliseconds(5);
  constexpr auto kMaxSettleWindow = std::chrono::milliseconds(50);

  const auto settleDeadline = std::chrono::steady_clock::now() + kMaxSettleWindow;
  bool warnedAboutMovingTarget = false;

  while (!stopRequested.load(std::memory_order_acquire))
  {
    const uint16_t target = dmd.GetUpdateQueuePosition();
    while (!stopRequested.load(std::memory_order_acquire) && !dmd.WaitForDumpers(target, 0))
    {
      if (std::chrono::steady_clock::now() >= settleDeadline)
      {
        if (!warnedAboutMovingTarget)
        {
          std::cout << "Dump settle timeout: queue kept moving, continuing with current output\n";
        }
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool stable = true;
    const auto quietDeadline = std::chrono::steady_clock::now() + kQuietWindow;
    while (!stopRequested.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < quietDeadline)
    {
      if (dmd.GetUpdateQueuePosition() != target)
      {
        stable = false;
        warnedAboutMovingTarget = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (stable)
    {
      return true;
    }

    if (std::chrono::steady_clock::now() >= settleDeadline)
    {
      if (warnedAboutMovingTarget)
      {
        std::cout << "Dump settle timeout: queue kept moving, continuing with current output\n";
      }
      return false;
    }
  }

  return false;
}

static void SendStartupWarmupFrame(DMDUtil::DMD& dmd, const Frame& frame, uint8_t indexedDepth)
{
  const uint32_t timestampMs = 0;

  if (frame.format == FrameFormat::RGB565)
  {
    std::vector<uint16_t> black(frame.width * frame.height, 0);
    dmd.UpdateRGB16DataWithTimestamp(black.data(), frame.width, frame.height, timestampMs, false);
    return;
  }

  if (frame.format == FrameFormat::RGB888)
  {
    std::vector<uint8_t> black(frame.width * frame.height * 3, 0);
    dmd.UpdateRGB24DataWithTimestamp(black.data(), frame.width, frame.height, timestampMs, false);
    return;
  }

  std::vector<uint8_t> black(frame.width * frame.height, 0);
  dmd.UpdateDataWithTimestamp(black.data(), indexedDepth, frame.width, frame.height, 255, 69, 0, timestampMs, false);
}

static bool FindLatestRgb565Dump(const std::string& dumpPath, const std::string& romName, std::string& outPath)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path root = dumpPath.empty() ? fs::path(".") : fs::path(dumpPath);
  if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
  {
    return false;
  }

  fs::file_time_type latestWrite{};
  bool found = false;
  const std::string prefix = romName + "-";
  for (const auto& entry : fs::directory_iterator(root, ec))
  {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) != 0) continue;
    if (!EndsWithCaseInsensitive(name, ".565.txt")) continue;
    const auto writeTime = entry.last_write_time(ec);
    if (ec) continue;
    if (!found || writeTime > latestWrite)
    {
      latestWrite = writeTime;
      outPath = entry.path().string();
      found = true;
    }
  }
  return found;
}

static bool WriteLiveJsonDump(const std::string& outputJsonPath, const std::string& inputPath,
                              const std::string& romName, const std::vector<LiveJsonFrameRecord>& frames)
{
  std::ofstream out(outputJsonPath, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }

  out << "{\n";
  out << "  \"schema\": \"dmdutil.playdump.live.v2\",\n";
  out << "  \"input\": \"" << EscapeJsonString(inputPath) << "\",\n";
  out << "  \"rom\": \"" << EscapeJsonString(romName) << "\",\n";
  out << "  \"frameCount\": " << frames.size() << ",\n";
  out << "  \"frames\": [\n";
  for (size_t i = 0; i < frames.size(); ++i)
  {
    const LiveJsonFrameRecord& frame = frames[i];
    out << "    {\"index\": " << frame.index << ", \"sourceFrameIndex\": " << frame.sourceFrameIndex
        << ", \"originalFrameIndex\": " << frame.originalFrameIndex
        << ", \"inputTimestampMs\": " << frame.inputTimestampMs << ", \"inputDurationMs\": " << frame.inputDurationMs
        << ", \"inputWidth\": " << frame.inputWidth << ", \"inputHeight\": " << frame.inputHeight
        << ", \"inputFormat\": \"" << FrameFormatToString(frame.inputFormat)
        << "\", \"inputCrc32\": " << frame.inputCrc32 << ", \"hasOutput\": " << (frame.hasOutput ? "true" : "false")
        << ", \"outputHasTimestamp\": " << (frame.hasOutputTimestamp ? "true" : "false")
        << ", \"outputTimestampMs\": " << frame.outputTimestampMs << ", \"outputWidth\": " << frame.outputWidth
        << ", \"outputHeight\": " << frame.outputHeight << ", \"outputMode\": \""
        << OutputModeToString(frame.outputMode) << "\", \"outputHashFNV1a64\": " << frame.outputHashFNV1a64
        << ", \"serumResult\": " << frame.serumResult << ", \"serumVersion\": " << frame.serumVersion
        << ", \"serumFrameId\": " << frame.serumFrameId << ", \"serumTriggerId\": " << frame.serumTriggerId
        << ", \"serumRotationTimer\": " << frame.serumRotationTimer
        << ", \"serumFeatureFlags\": " << frame.serumFeatureFlags << ", \"serumFeatures\": [";
    WriteFeatureFlagNames(out, frame.serumFeatureFlags);
    out << "]"
        << ", \"colorizeTimeUs\": " << frame.colorizeTimeUs
        << ", \"averageColorizeTimeUs\": " << frame.averageColorizeTimeUs << "}";
    if (i + 1 < frames.size())
    {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  return true;
}

static bool WriteJsonDump(const std::string& sourceDumpPath, const std::string& outputJsonPath,
                          const std::string& inputPath, const std::string& romName)
{
  uint8_t depth = 2;
  std::vector<Frame> frames;
  std::ifstream source(sourceDumpPath);
  if (!source)
  {
    return false;
  }
  if (!LoadRgb565DumpStream(source, depth, frames, false))
  {
    return false;
  }
  FinalizeFrameDurations(frames);

  std::ofstream out(outputJsonPath, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }

  out << "{\n";
  out << "  \"schema\": \"dmdutil.playdump.v1\",\n";
  out << "  \"input\": \"" << inputPath << "\",\n";
  out << "  \"rom\": \"" << romName << "\",\n";
  out << "  \"sourceDump565\": \"" << sourceDumpPath << "\",\n";
  out << "  \"frameCount\": " << frames.size() << ",\n";
  out << "  \"frames\": [\n";
  for (size_t i = 0; i < frames.size(); ++i)
  {
    const Frame& frame = frames[i];
    const uint64_t hash = HashFrameRgb565(frame.data16);
    out << "    {\"index\": " << i << ", \"timestampMs\": " << frame.timestampMs
        << ", \"durationMs\": " << frame.durationMs << ", \"width\": " << frame.width
        << ", \"height\": " << frame.height << ", \"format\": \"rgb565\", \"hashFNV1a64\": " << hash << "}";
    if (i + 1 < frames.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  return true;
}
}  // namespace

static struct cag_option options[] = {
    {.identifier = 'i',
     .access_letters = "i",
     .access_name = "input",
     .value_name = "FILE",
     .description = "Input dump file (.txt, .raw, .565.txt, .888.txt, or .zip)"},
    {.identifier = 'a',
     .access_letters = "a",
     .access_name = "alt-color-path",
     .value_name = "PATH",
     .description = "Alt color base path (optional, enables Serum colorization)"},
    {.identifier = 'd',
     .access_letters = "d",
     .access_name = "depth",
     .value_name = "VALUE",
     .description = "Bit depth to send (2 or 4) (optional, default is 2)"},
    {.identifier = 's',
     .access_letters = "s",
     .access_name = "server",
     .value_name = "HOST[:PORT]",
     .description = "Connect to a DMD server (optional)"},
    {.identifier = 'L', .access_letters = "L", .access_name = "no-local", .description = "Disable local displays"},
    {.identifier = 't', .access_letters = "t", .access_name = "dump-txt", .description = "Dump txt while playing"},
    {.identifier = '5', .access_letters = "5", .access_name = "dump-565", .description = "Dump rgb565 while playing"},
    {.identifier = '8', .access_letters = "8", .access_name = "dump-888", .description = "Dump rgb888 while playing"},
    {.identifier = 'z',
     .access_letters = "z",
     .access_name = "dump-zip",
     .description = "Write txt/565/888 dumps as .zip files"},
    {.identifier = 'w',
     .access_letters = "w",
     .access_name = "delay-ms",
     .value_name = "MS",
     .description = "Fixed delay between frames in milliseconds (optional, default is 100)"},
    {.identifier = 'W',
     .access_name = "startup-delay-ms",
     .value_name = "MS",
     .description = "Send one black warmup frame, then wait MS before playback to allow colorization loading"},
    {.identifier = 'l',
     .access_letters = "l",
     .access_name = "logging",
     .description = "Enable debug logging to stdout (optional, default is no logging)"},
    {.identifier = 'E',
     .access_name = "exclude-zedmd",
     .description = "Exclude ZeDMD from Serum/VNI colorization (optional)"},
    {.identifier = 'G',
     .access_name = "exclude-rgb24dmd",
     .description = "Exclude RGB24DMD from Serum/VNI colorization (optional)"},
    {.identifier = 'P',
     .access_name = "exclude-pin2dmd",
     .description = "Exclude PIN2DMD from Serum/VNI colorization (optional)"},
    {.identifier = 'C',
     .access_name = "exclude-pixelcade",
     .description = "Exclude Pixelcade from Serum/VNI colorization (optional)"},
    {.identifier = 'o',
     .access_letters = "o",
     .access_name = "dump-path",
     .value_name = "PATH",
     .description = "Output path for dumps (optional)"},
    {.identifier = 'r',
     .access_letters = "r",
     .access_name = "rom",
     .value_name = "NAME",
     .description = "ROM name for dumps (optional)"},
    {.identifier = 'x',
     .access_name = "crash-trace",
     .value_name = NULL,
     .description = "Enable in-process crash backtrace on fatal signals (optional)"},
    {.identifier = 'j',
     .access_letters = "j",
     .access_name = "dump-json",
     .value_name = "FILE",
     .description = "Write machine-readable JSON; uses live Serum metadata when colorization is active"},
    {.identifier = 'q',
     .access_name = "serum-profile",
     .description = "Enable libserum dynamic hotpath profiling logs (SERUM_PROFILE_DYNAMIC_HOTPATHS=1)"},
    {.identifier = 'Q',
     .access_name = "serum-profile-sparse",
     .description = "Enable libserum dynamic+sparse profiling logs (SERUM_PROFILE_DYNAMIC_HOTPATHS=1, "
                    "SERUM_PROFILE_SPARSE_VECTORS=1)"},
    {.identifier = 'k',
     .access_name = "coverage-json",
     .value_name = "FILE",
     .description = "Write coverage checklist JSON (first occurrences plus optional transition windows)"},
    {.identifier = 'T',
     .access_name = "coverage-transition-tail",
     .value_name = "N",
     .description = "Coverage export: include N following frames for each first-seen signature transition"},
    {.identifier = 'M',
     .access_name = "coverage-max-frames",
     .value_name = "N",
     .description = "Coverage export: cap number of selected frame indices (0 = unlimited)"},
    {.identifier = 'S',
     .access_name = "start-frame",
     .value_name = "N",
     .description = "Replay/dump only frames starting at zero-based frame index N"},
    {.identifier = 'U',
     .access_name = "end-frame",
     .value_name = "N",
     .description = "Replay/dump only frames up to zero-based frame index N"},
    {.identifier = 'R', .access_letters = "R", .access_name = "raw", .description = "Force raw dump parsing"},
    {.identifier = 'h', .access_letters = "h", .access_name = "help", .description = "Show help"}};

int main(int argc, char* argv[])
{
  char identifier;
  cag_option_context cag_context;

  const char* opt_input = nullptr;
  const char* opt_alt_color_path = nullptr;
  const char* opt_server = nullptr;
  const char* opt_dump_path = nullptr;
  const char* opt_dump_json = nullptr;
  const char* opt_coverage_json = nullptr;
  const char* opt_rom = nullptr;
  uint32_t opt_coverage_transition_tail = 0;
  uint32_t opt_coverage_max_frames = 0;
  uint32_t opt_start_frame = 0;
  uint32_t opt_end_frame = std::numeric_limits<uint32_t>::max();
  uint8_t opt_depth = 2;
  bool opt_no_local = false;
  bool opt_dump_txt = false;
  bool opt_dump_565 = false;
  bool opt_dump_888 = false;
  bool opt_dump_zip = false;
  bool opt_force_raw = false;
  bool opt_serum_profile = false;
  bool opt_serum_profile_sparse = false;
  uint32_t opt_delay_ms = 100;
  uint32_t opt_startup_delay_ms = 0;
  bool opt_delay_set = true;
  bool opt_crash_trace = false;

  cag_option_init(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    identifier = cag_option_get_identifier(&cag_context);
    switch (identifier)
    {
      case 'i':
        opt_input = cag_option_get_value(&cag_context);
        break;
      case 'a':
        opt_alt_color_path = cag_option_get_value(&cag_context);
        break;
      case 'd':
        opt_depth = (uint8_t)atoi(cag_option_get_value(&cag_context));
        break;
      case 's':
        opt_server = cag_option_get_value(&cag_context);
        break;
      case 'L':
        opt_no_local = true;
        break;
      case 't':
        opt_dump_txt = true;
        break;
      case '5':
        opt_dump_565 = true;
        break;
      case '8':
        opt_dump_888 = true;
        break;
      case 'z':
        opt_dump_zip = true;
        break;
      case 'w':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0) opt_delay_ms = (uint32_t)value;
        }
        break;
      }
      case 'W':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0) opt_startup_delay_ms = (uint32_t)value;
        }
        break;
      }
      case 'o':
        opt_dump_path = cag_option_get_value(&cag_context);
        break;
      case 'l':
        DMDUtil::Config::GetInstance()->SetLogCallback(LogToStdoutCallback);
        DMDUtil::Config::GetInstance()->SetLogLevel(DMDUtil_LogLevel_DEBUG);
        break;
      case 'E':
        DMDUtil::Config::GetInstance()->SetExcludeColorizedFramesForZeDMD(true);
        break;
      case 'G':
        DMDUtil::Config::GetInstance()->SetExcludeColorizedFramesForRGB24DMD(true);
        break;
      case 'P':
        DMDUtil::Config::GetInstance()->SetExcludeColorizedFramesForPIN2DMD(true);
        break;
      case 'C':
        DMDUtil::Config::GetInstance()->SetExcludeColorizedFramesForPixelcade(true);
        break;
      case 'r':
        opt_rom = cag_option_get_value(&cag_context);
        break;
      case 'R':
        opt_force_raw = true;
        break;
      case 'x':
        opt_crash_trace = true;
        break;
      case 'j':
        opt_dump_json = cag_option_get_value(&cag_context);
        break;
      case 'q':
        opt_serum_profile = true;
        break;
      case 'Q':
        opt_serum_profile_sparse = true;
        break;
      case 'k':
        opt_coverage_json = cag_option_get_value(&cag_context);
        break;
      case 'T':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0)
          {
            opt_coverage_transition_tail = static_cast<uint32_t>(value);
          }
        }
        break;
      }
      case 'M':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0)
          {
            opt_coverage_max_frames = static_cast<uint32_t>(value);
          }
        }
        break;
      }
      case 'S':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0)
          {
            opt_start_frame = static_cast<uint32_t>(value);
          }
        }
        break;
      }
      case 'U':
      {
        const char* valueStr = cag_option_get_value(&cag_context);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value >= 0)
          {
            opt_end_frame = static_cast<uint32_t>(value);
          }
        }
        break;
      }
      case 'h':
        std::cerr << "Usage: " << argv[0] << " [OPTION]...\n";
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
    }
  }

  if (opt_crash_trace)
  {
    InstallCrashTraceHandlers();
  }

  std::signal(SIGINT, HandleSigInt);

  if (!opt_input)
  {
    std::cerr << "Error: Missing input file\n";
    std::cerr << "Usage: " << argv[0] << " -i MY_DUMP_FILE.txt\n";
    cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
    return 1;
  }

  if (opt_depth != 2 && opt_depth != 4)
  {
    std::cerr << "Error: Depth must be 2 or 4\n";
    return 1;
  }
  if (opt_dump_json && opt_dump_json[0] == '\0')
  {
    std::cerr << "Error: --dump-json requires a non-empty file path\n";
    return 1;
  }
  if (opt_coverage_json && opt_coverage_json[0] == '\0')
  {
    std::cerr << "Error: --coverage-json requires a non-empty file path\n";
    return 1;
  }
  if (opt_dump_json && opt_dump_zip)
  {
    std::cerr << "Error: --dump-json currently does not support --dump-zip\n";
    return 1;
  }
  const bool liveJsonRequested = opt_dump_json && opt_alt_color_path && opt_alt_color_path[0] != '\0';
  if (opt_dump_json)
  {
    opt_dump_565 = true;
  }
  if (opt_end_frame < opt_start_frame)
  {
    std::cerr << "Error: --end-frame must be greater than or equal to --start-frame\n";
    return 1;
  }

  if (opt_serum_profile || opt_serum_profile_sparse)
  {
    SetEnvFlag("SERUM_PROFILE_DYNAMIC_HOTPATHS", "1");
  }
  if (opt_serum_profile_sparse)
  {
    SetEnvFlag("SERUM_PROFILE_SPARSE_VECTORS", "1");
  }

  std::string inputPath = opt_input;
  InputFormat format = InputFormat::Txt;
  if (IsZipFile(inputPath))
  {
    format = InputFormat::Zip;
  }
  else if (opt_force_raw || EndsWithCaseInsensitive(inputPath, ".raw"))
  {
    format = InputFormat::Raw;
  }
  else if (EndsWithCaseInsensitive(inputPath, ".565.txt"))
  {
    format = InputFormat::Rgb565;
  }
  else if (EndsWithCaseInsensitive(inputPath, ".888.txt"))
  {
    format = InputFormat::Rgb888;
  }

  std::vector<Frame> frames;
  switch (format)
  {
    case InputFormat::Raw:
      if (!LoadRawDump(inputPath, opt_depth, frames)) return 1;
      break;
    case InputFormat::Rgb565:
      if (!LoadRgb565Dump(inputPath, opt_depth, frames)) return 1;
      break;
    case InputFormat::Rgb888:
      if (!LoadRgb888Dump(inputPath, opt_depth, frames)) return 1;
      break;
    case InputFormat::Zip:
      if (!LoadZipDump(inputPath, opt_depth, frames)) return 1;
      break;
    case InputFormat::Txt:
    default:
      if (!LoadTxtDump(inputPath, opt_depth, frames)) return 1;
      break;
  }
  FinalizeFrameDurations(frames);

  std::vector<uint64_t> frameInputSignatures;
  frameInputSignatures.reserve(frames.size());
  std::vector<uint32_t> playbackOriginalIndices;
  playbackOriginalIndices.reserve(frames.size());
  for (size_t i = 0; i < frames.size(); ++i)
  {
    playbackOriginalIndices.push_back(static_cast<uint32_t>(i));
  }
  for (const Frame& frame : frames)
  {
    frameInputSignatures.push_back(HashFrameInputSignature(frame));
  }

  if (!frames.empty() && (opt_start_frame > 0 || opt_end_frame != std::numeric_limits<uint32_t>::max()))
  {
    const size_t originalCount = frames.size();
    const size_t startIndex = std::min<size_t>(opt_start_frame, frames.size());
    const size_t endIndexExclusive = std::min<size_t>(frames.size(), static_cast<size_t>(opt_end_frame) + 1);

    if (startIndex >= endIndexExclusive)
    {
      frames.clear();
      frameInputSignatures.clear();
      playbackOriginalIndices.clear();
    }
    else
    {
      frames = std::vector<Frame>(frames.begin() + static_cast<std::ptrdiff_t>(startIndex),
                                  frames.begin() + static_cast<std::ptrdiff_t>(endIndexExclusive));
      frameInputSignatures =
          std::vector<uint64_t>(frameInputSignatures.begin() + static_cast<std::ptrdiff_t>(startIndex),
                                frameInputSignatures.begin() + static_cast<std::ptrdiff_t>(endIndexExclusive));
      playbackOriginalIndices =
          std::vector<uint32_t>(playbackOriginalIndices.begin() + static_cast<std::ptrdiff_t>(startIndex),
                                playbackOriginalIndices.begin() + static_cast<std::ptrdiff_t>(endIndexExclusive));
    }

    std::cout << "Frame window selected [" << opt_start_frame << ", ";
    if (opt_end_frame == std::numeric_limits<uint32_t>::max())
    {
      std::cout << "end";
    }
    else
    {
      std::cout << opt_end_frame;
    }
    std::cout << "], kept " << frames.size() << " frames out of " << originalCount << "\n";
  }

  std::string romName;
  if (opt_rom && opt_rom[0] != '\0')
  {
    romName = opt_rom;
  }
  else
  {
    romName = StripExtension(GetBaseName(inputPath));
  }

  if (romName.empty()) romName = "dump";
  if (romName.size() > DMDUTIL_MAX_NAME_SIZE - 1)
  {
    romName.resize(DMDUTIL_MAX_NAME_SIZE - 1);
  }

  DMDUtil::Config* config = DMDUtil::Config::GetInstance();
  if (opt_alt_color_path && opt_alt_color_path[0] != '\0')
  {
    config->SetAltColor(true);
    config->SetAltColorPath(opt_alt_color_path);
  }
  if (opt_server && opt_server[0] != '\0')
  {
    std::string host;
    int port = 6789;
    ParseServer(opt_server, host, port);
    config->SetDMDServer(true);
    config->SetDMDServerAddr(host.c_str());
    config->SetDMDServerPort(port);
    config->SetLocalDisplaysActive(!opt_no_local);
  }
  else if (opt_no_local)
  {
    config->SetLocalDisplaysActive(false);
  }

  DMDUtil::DMD dmd;
  dmd.SetRomName(romName.c_str());
  if (opt_alt_color_path && opt_alt_color_path[0] != '\0')
  {
    dmd.SetAltColorPath(opt_alt_color_path);
  }
  dmd.FindDisplays();

  for (int i = 0; i < 100 && DMDUtil::DMD::IsFinding(); ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  const auto dumpStartTime = std::chrono::steady_clock::now();
  std::vector<LiveJsonFrameRecord> liveJsonFrames;
  bool liveJsonActive = liveJsonRequested;
  bool liveJsonFallback = false;
  if (liveJsonRequested)
  {
    liveJsonFrames.reserve(frames.size());
    std::cout << "Live JSON capture requested for Serum playback metadata\n";
  }
  if (opt_startup_delay_ms > 0 && !frames.empty())
  {
    SendStartupWarmupFrame(dmd, frames.front(), opt_depth);
    std::cout << "Startup warmup: sent black frame " << frames.front().width << "x" << frames.front().height
              << ", waiting " << opt_startup_delay_ms << "ms for colorization load\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(opt_startup_delay_ms));
  }
  if (opt_dump_txt || opt_dump_565 || opt_dump_888)
  {
    if (opt_dump_path && opt_dump_path[0] != '\0')
    {
      config->SetDumpPath(opt_dump_path);
    }
    if (opt_dump_zip)
    {
      config->SetDumpZip(true);
    }
    if (opt_dump_txt) dmd.DumpDMDTxt();
    if (opt_dump_565) dmd.DumpDMDRgb565();
    if (opt_dump_888) dmd.DumpDMDRgb888();
  }

  const uint8_t dumpR = 255;
  const uint8_t dumpG = 69;
  const uint8_t dumpB = 0;
  const bool dumpEnabled = opt_dump_txt || opt_dump_565 || opt_dump_888;
  const bool serumProfilingEnabled = opt_serum_profile || opt_serum_profile_sparse;
  uint64_t peakRssBytes = 0;
  uint32_t profiledFrames = 0;
  const size_t totalFramesToPlay = frames.size();
  const uint64_t estimatedPlaybackMs = ComputeEstimatedPlaybackMs(frames, opt_delay_set, opt_delay_ms);
  uint64_t playedPlannedMs = 0;
  size_t playedFramesCount = 0;
  std::cout << "Playback start: " << totalFramesToPlay << " frames"
            << ", estimated duration=" << FormatDurationMs(estimatedPlaybackMs) << "\n";
  auto lastProgressLog = std::chrono::steady_clock::now();

  for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
  {
    const Frame& frame = frames[frameIndex];
    if (g_stopRequested.load(std::memory_order_acquire))
    {
      break;
    }

    const uint32_t queueTimestamp = frame.originalTimestampMs;
    DMDUtil::DMD::FrameContext frameContext;
    frameContext.valid = true;
    frameContext.sourceOrdinal = frameIndex;
    frameContext.sourceFrameIndex = static_cast<uint32_t>(frameIndex);
    frameContext.originalFrameIndex = frameIndex < playbackOriginalIndices.size() ? playbackOriginalIndices[frameIndex]
                                                                                  : static_cast<uint32_t>(frameIndex);
    frameContext.inputCrc32 = ComputeInputCrc32(frame);
    frameContext.inputTimestampMs = frame.originalTimestampMs;
    frameContext.inputDurationMs = frame.durationMs;

    if (frame.format == FrameFormat::RGB565)
    {
      if (!frame.data16.empty())
      {
        if (liveJsonRequested)
        {
          dmd.UpdateRGB16DataWithMetadataAndTimestamp(frame.data16.data(), frame.width, frame.height, queueTimestamp,
                                                      frameContext, false);
        }
        else
        {
          dmd.UpdateRGB16DataWithTimestamp(frame.data16.data(), frame.width, frame.height, queueTimestamp, false);
        }
      }
    }
    else if (frame.format == FrameFormat::RGB888)
    {
      if (!frame.data.empty())
      {
        if (liveJsonRequested)
        {
          dmd.UpdateRGB24DataWithMetadataAndTimestamp(frame.data.data(), frame.width, frame.height, queueTimestamp,
                                                      frameContext, false);
        }
        else
        {
          dmd.UpdateRGB24DataWithTimestamp(frame.data.data(), frame.width, frame.height, queueTimestamp, false);
        }
      }
    }
    else if (!frame.data.empty())
    {
      if (liveJsonRequested)
      {
        dmd.UpdateDataWithMetadataAndTimestamp(frame.data.data(), opt_depth, frame.width, frame.height, dumpR, dumpG,
                                               dumpB, queueTimestamp, frameContext, false);
      }
      else
      {
        dmd.UpdateDataWithTimestamp(frame.data.data(), opt_depth, frame.width, frame.height, dumpR, dumpG, dumpB,
                                    queueTimestamp, false);
      }
    }

    if (liveJsonActive)
    {
      DMDUtil::DMD::SerumCapture capture;
      const uint32_t captureTimeoutMs = (frameIndex == 0) ? 5000u : 250u;
      if (dmd.WaitForSerumColorizeCapture(frameContext.sourceOrdinal, capture, captureTimeoutMs))
      {
        liveJsonFrames.push_back(MakeLiveJsonFrameRecord(static_cast<uint32_t>(frameIndex), frame, capture));
      }
      else
      {
        liveJsonActive = false;
        liveJsonFallback = true;
        liveJsonFrames.clear();
        std::cout << "Live JSON fallback: no Serum capture for playback frame " << frameIndex
                  << ", using generated .565 dump instead\n";
      }
    }

    if (dumpEnabled)
    {
      WaitForDumpersToSettle(dmd, g_stopRequested);
    }

    const uint32_t sleepMs = static_cast<uint32_t>(ComputePlannedSleepMsForFrame(frame, opt_delay_set, opt_delay_ms));

    if (sleepMs > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    playedPlannedMs += sleepMs;
    playedFramesCount = frameIndex + 1;

    const size_t playedFrames = frameIndex + 1;
    const bool periodicFrameLog = (playedFrames % 250) == 0;
    const bool periodicTimeLog = std::chrono::steady_clock::now() - lastProgressLog >= std::chrono::seconds(1);
    if (playedFrames == totalFramesToPlay || periodicFrameLog || periodicTimeLog)
    {
      const double percent = totalFramesToPlay > 0
                                 ? (100.0 * static_cast<double>(playedFrames) / static_cast<double>(totalFramesToPlay))
                                 : 100.0;
      const uint64_t remainingMs = estimatedPlaybackMs > playedPlannedMs ? (estimatedPlaybackMs - playedPlannedMs) : 0;
      std::cout << "Playback progress: " << playedFrames << "/" << totalFramesToPlay << " (" << percent
                << "%), eta=" << FormatDurationMs(remainingMs) << "\n";
      lastProgressLog = std::chrono::steady_clock::now();
    }

    if (serumProfilingEnabled)
    {
      const uint64_t rssBytes = GetProcessRssBytes();
      if (rssBytes > peakRssBytes)
      {
        peakRssBytes = rssBytes;
      }
      ++profiledFrames;
      if (profiledFrames % 240 == 0)
      {
        std::cout << "Profile RAM: rssMB=" << (rssBytes / (1024.0 * 1024.0))
                  << " peakMB=" << (peakRssBytes / (1024.0 * 1024.0)) << " frames=" << profiledFrames << "\n";
      }
    }
  }

  if (g_stopRequested.load(std::memory_order_acquire))
  {
    uint16_t target = dmd.GetUpdateQueuePosition();
    for (int i = 0; i < 10; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      uint16_t current = dmd.GetUpdateQueuePosition();
      if (current == target) break;
      target = current;
    }
    dmd.WaitForDumpers(target, 2000);
    std::cout << "Playback interrupted by SIGINT after " << playedFramesCount << "/" << totalFramesToPlay
              << " frames\n";
  }

  if (opt_dump_json)
  {
    if (liveJsonRequested && !liveJsonFallback && liveJsonFrames.size() == playedFramesCount)
    {
      if (!WriteLiveJsonDump(opt_dump_json, inputPath, romName, liveJsonFrames))
      {
        std::cerr << "Error: Failed to write live JSON dump " << opt_dump_json << "\n";
        return 1;
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dumpStartTime)
              .count();
      std::cout << "Live JSON dump written to " << opt_dump_json << " (frames=" << liveJsonFrames.size()
                << ", elapsed=" << elapsed << "ms)\n";
    }
    else
    {
      std::string latestRgb565DumpPath;
      const std::string dumpDir = (opt_dump_path && opt_dump_path[0] != '\0') ? opt_dump_path : ".";
      if (!FindLatestRgb565Dump(dumpDir, romName, latestRgb565DumpPath))
      {
        std::cerr << "Error: Failed to locate generated .565.txt dump for ROM " << romName << "\n";
        return 1;
      }
      if (!WriteJsonDump(latestRgb565DumpPath, opt_dump_json, inputPath, romName))
      {
        std::cerr << "Error: Failed to write JSON dump " << opt_dump_json << "\n";
        return 1;
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dumpStartTime)
              .count();
      std::cout << "JSON dump written to " << opt_dump_json << " (source: " << latestRgb565DumpPath
                << ", elapsed=" << elapsed << "ms)\n";
    }
  }

  if (opt_coverage_json && opt_coverage_json[0] != '\0')
  {
    std::vector<Frame> coverageFrames;
    std::vector<uint64_t> coverageSignatures;
    if (playedFramesCount > 0 && playedFramesCount < frames.size())
    {
      coverageFrames.assign(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(playedFramesCount));
      coverageSignatures.assign(frameInputSignatures.begin(),
                                frameInputSignatures.begin() + static_cast<std::ptrdiff_t>(playedFramesCount));
    }
    else
    {
      coverageFrames = frames;
      coverageSignatures = frameInputSignatures;
    }

    const std::vector<LiveJsonFrameRecord>* coverageLiveFrames = nullptr;
    std::vector<LiveJsonFrameRecord> partialLiveJsonFrames;
    if (!liveJsonFrames.empty())
    {
      if (playedFramesCount > 0 && playedFramesCount < liveJsonFrames.size())
      {
        partialLiveJsonFrames.assign(liveJsonFrames.begin(),
                                     liveJsonFrames.begin() + static_cast<std::ptrdiff_t>(playedFramesCount));
        coverageLiveFrames = &partialLiveJsonFrames;
      }
      else
      {
        coverageLiveFrames = &liveJsonFrames;
      }
    }

    FrameFormat coverageFormat = FrameFormat::Indexed;
    if (!FramesShareFormat(coverageFrames, coverageFormat))
    {
      std::cerr << "Error: Coverage export frames do not share a single text dump format\n";
      return 1;
    }

    const std::string coverageDumpPath = BuildCoverageDumpPath(opt_coverage_json, coverageFormat);
    const std::vector<uint32_t> selectedCoverageIndices = BuildCoverageSelectedIndices(
        coverageFrames, coverageSignatures, opt_coverage_transition_tail, opt_coverage_max_frames, coverageLiveFrames);

    if (!WriteCoverageJson(opt_coverage_json, inputPath, coverageFrames, coverageSignatures,
                           opt_coverage_transition_tail, opt_coverage_max_frames, coverageLiveFrames,
                           &coverageDumpPath))
    {
      std::cerr << "Error: Failed to write coverage JSON " << opt_coverage_json << "\n";
      return 1;
    }
    std::vector<Frame> selectedCoverageFrames;
    if (!selectedCoverageIndices.empty())
    {
      selectedCoverageFrames.reserve(selectedCoverageIndices.size());
      size_t selectedPos = 0;
      for (size_t i = 0; i < coverageFrames.size() && selectedPos < selectedCoverageIndices.size(); ++i)
      {
        if (selectedCoverageIndices[selectedPos] == i)
        {
          selectedCoverageFrames.push_back(coverageFrames[i]);
          ++selectedPos;
        }
      }
    }
    else
    {
      selectedCoverageFrames = coverageFrames;
    }

    if (!WriteFilteredTextDump(coverageDumpPath, selectedCoverageFrames))
    {
      std::cerr << "Error: Failed to write coverage dump " << coverageDumpPath << "\n";
      return 1;
    }

    std::cout << "Coverage JSON written to " << opt_coverage_json << " (" << coverageFrames.size()
              << " input frames)\n";
    std::cout << "Coverage dump written to " << coverageDumpPath << " (" << selectedCoverageFrames.size()
              << " selected frames)\n";
  }

  if (serumProfilingEnabled)
  {
    const uint64_t rssBytes = GetProcessRssBytes();
    if (rssBytes > peakRssBytes)
    {
      peakRssBytes = rssBytes;
    }
    std::cout << "Profile RAM final: rssMB=" << (rssBytes / (1024.0 * 1024.0))
              << " peakMB=" << (peakRssBytes / (1024.0 * 1024.0)) << " frames=" << profiledFrames << "\n";
  }

  std::cout << "Playback finished: " << playedFramesCount << "/" << totalFramesToPlay << " frames processed\n";

  return 0;
}
