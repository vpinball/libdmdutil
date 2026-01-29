#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "cargs.h"

namespace
{
std::atomic<bool> g_stopRequested{false};

void HandleSigInt(int)
{
  g_stopRequested.store(true, std::memory_order_release);
}

enum class FrameFormat
{
  Indexed,
  RGB565,
  RGB888
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

static bool LoadTxtDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

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

static bool LoadRgb565Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  (void)outDepth;
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

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

    if ((line.size() % 4) != 0)
    {
      std::cerr << "Error: Invalid line width in rgb565 dump\n";
      return false;
    }

    uint16_t lineWidth = (uint16_t)(line.size() / 4);
    if (width == 0)
    {
      width = lineWidth;
    }
    else if (lineWidth != width)
    {
      std::cerr << "Error: Inconsistent line width in rgb565 dump\n";
      return false;
    }

    for (size_t pos = 0; pos < line.size(); pos += 4)
    {
      uint16_t value = 0;
      if (!ParseHexUint16(line, pos, value))
      {
        std::cerr << "Error: Invalid hex digit in rgb565 dump\n";
        return false;
      }
      current.data16.push_back(value);
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

static bool LoadRgb888Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames)
{
  (void)outDepth;
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "Error: Unable to open input file: " << path << "\n";
    return false;
  }

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
}  // namespace

static struct cag_option options[] = {
    {.identifier = 'i',
     .access_letters = "i",
     .access_name = "input",
     .value_name = "FILE",
     .description = "Input dump file (.txt or .raw)"},
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
    {.identifier = 'w',
     .access_letters = "w",
     .access_name = "delay-ms",
     .value_name = "MS",
     .description = "Fixed delay between frames in milliseconds (optional, default is 100)"},
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
  const char* opt_rom = nullptr;
  uint8_t opt_depth = 2;
  bool opt_no_local = false;
  bool opt_dump_txt = false;
  bool opt_dump_565 = false;
  bool opt_dump_888 = false;
  bool opt_force_raw = false;
  uint32_t opt_delay_ms = 100;
  bool opt_delay_set = true;

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
      case 'o':
        opt_dump_path = cag_option_get_value(&cag_context);
        break;
      case 'r':
        opt_rom = cag_option_get_value(&cag_context);
        break;
      case 'R':
        opt_force_raw = true;
        break;
      case 'h':
        std::cerr << "Usage: " << argv[0] << " [OPTION]...\n";
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
    }
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

  std::string inputPath = opt_input;
  enum class InputFormat
  {
    Txt,
    Raw,
    Rgb565,
    Rgb888
  };
  InputFormat format = InputFormat::Txt;
  if (opt_force_raw || EndsWithCaseInsensitive(inputPath, ".raw"))
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
    case InputFormat::Txt:
    default:
      if (!LoadTxtDump(inputPath, opt_depth, frames)) return 1;
      break;
  }
  FinalizeFrameDurations(frames);

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

  if (opt_dump_txt || opt_dump_565 || opt_dump_888)
  {
    if (opt_dump_path && opt_dump_path[0] != '\0')
    {
      config->SetDumpPath(opt_dump_path);
    }
    if (opt_dump_txt) dmd.DumpDMDTxt();
    if (opt_dump_565) dmd.DumpDMDRgb565();
    if (opt_dump_888) dmd.DumpDMDRgb888();
  }

  const uint8_t dumpR = 255;
  const uint8_t dumpG = 69;
  const uint8_t dumpB = 0;
  const bool dumpEnabled = opt_dump_txt || opt_dump_565 || opt_dump_888;

  if (opt_delay_ms < 100)
  {
    opt_delay_ms = 100;
  }

  for (const Frame& frame : frames)
  {
    if (g_stopRequested.load(std::memory_order_acquire))
    {
      break;
    }
    const uint32_t queueTimestamp = frame.originalTimestampMs;

    if (frame.format == FrameFormat::RGB565)
    {
      if (!frame.data16.empty())
      {
        dmd.UpdateRGB16DataWithTimestamp(frame.data16.data(), frame.width, frame.height, queueTimestamp, false);
      }
    }
    else if (frame.format == FrameFormat::RGB888)
    {
      if (!frame.data.empty())
      {
        dmd.UpdateRGB24DataWithTimestamp(frame.data.data(), frame.width, frame.height, queueTimestamp, false);
      }
    }
    else if (!frame.data.empty())
    {
      dmd.UpdateDataWithTimestamp(frame.data.data(), opt_depth, frame.width, frame.height, dumpR, dumpG, dumpB,
                                  queueTimestamp, false);
    }

    if (dumpEnabled)
    {
      bool settled = false;
      while (!g_stopRequested.load(std::memory_order_acquire) && !settled)
      {
        uint16_t target = dmd.GetUpdateQueuePosition();
        while (!g_stopRequested.load(std::memory_order_acquire) && !dmd.WaitForDumpers(target, 0))
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        settled = true;
        auto quietStart = std::chrono::steady_clock::now();
        while (!g_stopRequested.load(std::memory_order_acquire))
        {
          if (dmd.GetUpdateQueuePosition() != target)
          {
            settled = false;
            break;
          }
          if (std::chrono::steady_clock::now() - quietStart >= std::chrono::milliseconds(5))
          {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    }

    uint32_t sleepMs = 0;
    if (opt_delay_set)
    {
      sleepMs = opt_delay_ms;
      if (frame.durationMs > 0 && frame.durationMs < sleepMs)
      {
        sleepMs = frame.durationMs;
      }
    }
    else
    {
      sleepMs = frame.durationMs;
    }

    if (sleepMs > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
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
  }

  return 0;
}
