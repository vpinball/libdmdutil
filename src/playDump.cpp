#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "FrameUtil.h"
#include "cargs.h"

namespace
{
struct Frame
{
  uint32_t durationMs = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint8_t> data;
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

static void GenerateRandomSuffix(char* buffer, size_t length)
{
  static bool seedDone = false;
  if (!seedDone)
  {
    srand(static_cast<unsigned int>(time(nullptr)));
    seedDone = true;
  }

  const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  size_t charsetSize = sizeof(charset) - 1;

  for (size_t i = 0; i < length; ++i)
  {
    buffer[i] = charset[rand() % charsetSize];
  }
  buffer[length] = '\0';
}

static std::string BuildDumpPath(const std::string& dir, const std::string& rom, const std::string& suffix,
                                 const std::string& extension)
{
  if (dir.empty())
  {
    return "./" + rom + "-" + suffix + extension;
  }

  const char last = dir.back();
  if (last == '/' || last == '\\')
  {
    return dir + rom + "-" + suffix + extension;
  }

  return dir + "/" + rom + "-" + suffix + extension;
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

static void BuildPalette(uint8_t* palette, uint8_t depth, uint8_t r, uint8_t g, uint8_t b)
{
  if (depth == 0 || depth > 8) return;
  const uint8_t colors = (uint8_t)(1u << depth);
  for (uint8_t i = 0; i < colors; ++i)
  {
    float perc = FrameUtil::Helper::CalcBrightness((float)i / (float)(colors - 1));
    palette[i * 3] = (uint8_t)((float)r * perc);
    palette[i * 3 + 1] = (uint8_t)((float)g * perc);
    palette[i * 3 + 2] = (uint8_t)((float)b * perc);
  }
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
      current.durationMs = (uint32_t)strtoul(line.c_str(), nullptr, 16);
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

static bool ConvertUpdateToIndexed(const DMDUtil::DMD::Update& update, uint8_t outDepth, Frame& frame)
{
  using Mode = DMDUtil::DMD::Mode;
  frame.width = update.width;
  frame.height = update.height;
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
    uint32_t duration = 0;
    uint32_t size = 0;
    if (!file.read(reinterpret_cast<char*>(&duration), sizeof(duration))) break;
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
    frame.durationMs = duration;
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

static void WriteTxtFrame(FILE* f, const Frame& frame)
{
  fprintf(f, "0x%08x\r\n", frame.durationMs);
  for (uint16_t y = 0; y < frame.height; ++y)
  {
    for (uint16_t x = 0; x < frame.width; ++x)
    {
      fprintf(f, "%x", frame.data[y * frame.width + x]);
    }
    fprintf(f, "\r\n");
  }
  fprintf(f, "\r\n");
}

static void WriteRgb565Frame(FILE* f, const Frame& frame, const uint8_t* palette)
{
  const int length = (int)frame.width * frame.height;
  std::vector<uint8_t> rgb24(length * 3);
  FrameUtil::Helper::ConvertToRgb24(rgb24.data(), const_cast<uint8_t*>(frame.data.data()), length,
                                   const_cast<uint8_t*>(palette));

  fprintf(f, "0x%08x\r\n", frame.durationMs);
  for (uint16_t y = 0; y < frame.height; ++y)
  {
    for (uint16_t x = 0; x < frame.width; ++x)
    {
      int pos = (y * frame.width + x) * 3;
      uint32_t r = rgb24[pos];
      uint32_t g = rgb24[pos + 1];
      uint32_t b = rgb24[pos + 2];
      uint16_t value = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
      fprintf(f, "%04x", value);
    }
    fprintf(f, "\r\n");
  }
  fprintf(f, "\r\n");
}

static void WriteRgb888Frame(FILE* f, const Frame& frame, const uint8_t* palette)
{
  const int length = (int)frame.width * frame.height;
  std::vector<uint8_t> rgb24(length * 3);
  FrameUtil::Helper::ConvertToRgb24(rgb24.data(), const_cast<uint8_t*>(frame.data.data()), length,
                                   const_cast<uint8_t*>(palette));

  fprintf(f, "0x%08x\r\n", frame.durationMs);
  for (uint16_t y = 0; y < frame.height; ++y)
  {
    for (uint16_t x = 0; x < frame.width; ++x)
    {
      int pos = (y * frame.width + x) * 3;
      fprintf(f, "%02x%02x%02x", rgb24[pos], rgb24[pos + 1], rgb24[pos + 2]);
    }
    fprintf(f, "\r\n");
  }
  fprintf(f, "\r\n");
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
  const char* opt_server = nullptr;
  const char* opt_dump_path = nullptr;
  const char* opt_rom = nullptr;
  uint8_t opt_depth = 2;
  bool opt_no_local = false;
  bool opt_dump_txt = false;
  bool opt_dump_565 = false;
  bool opt_dump_888 = false;
  bool opt_force_raw = false;

  cag_option_init(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    identifier = cag_option_get_identifier(&cag_context);
    switch (identifier)
    {
      case 'i':
        opt_input = cag_option_get_value(&cag_context);
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
  bool useRaw = opt_force_raw || EndsWithCaseInsensitive(inputPath, ".raw");

  std::vector<Frame> frames;
  if (useRaw)
  {
    if (!LoadRawDump(inputPath, opt_depth, frames)) return 1;
  }
  else
  {
    if (!LoadTxtDump(inputPath, opt_depth, frames)) return 1;
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
  dmd.FindDisplays();

  for (int i = 0; i < 100 && DMDUtil::DMD::IsFinding(); ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  FILE* dumpTxt = nullptr;
  FILE* dump565 = nullptr;
  FILE* dump888 = nullptr;
  uint8_t palette[256 * 3] = {0};
  const uint8_t dumpR = 255;
  const uint8_t dumpG = 69;
  const uint8_t dumpB = 0;
  BuildPalette(palette, opt_depth, dumpR, dumpG, dumpB);

  if (opt_dump_txt || opt_dump_565 || opt_dump_888)
  {
    char suffix[9];
    GenerateRandomSuffix(suffix, 8);
    std::string dumpDir = opt_dump_path ? opt_dump_path : "";

    if (opt_dump_txt)
    {
      std::string path = BuildDumpPath(dumpDir, romName, suffix, ".txt");
      dumpTxt = fopen(path.c_str(), "w");
      if (!dumpTxt)
      {
        std::cerr << "Error: Failed to open txt dump file: " << path << "\n";
        return 1;
      }
    }

    if (opt_dump_565)
    {
      std::string path = BuildDumpPath(dumpDir, romName, suffix, ".565.txt");
      dump565 = fopen(path.c_str(), "w");
      if (!dump565)
      {
        std::cerr << "Error: Failed to open rgb565 dump file: " << path << "\n";
        return 1;
      }
    }

    if (opt_dump_888)
    {
      std::string path = BuildDumpPath(dumpDir, romName, suffix, ".888.txt");
      dump888 = fopen(path.c_str(), "w");
      if (!dump888)
      {
        std::cerr << "Error: Failed to open rgb888 dump file: " << path << "\n";
        return 1;
      }
    }
  }

  for (const Frame& frame : frames)
  {
    if (!frame.data.empty())
    {
      dmd.UpdateData(frame.data.data(), opt_depth, frame.width, frame.height, dumpR, dumpG, dumpB, false);
    }

    if (dumpTxt)
    {
      WriteTxtFrame(dumpTxt, frame);
    }
    if (dump565)
    {
      WriteRgb565Frame(dump565, frame, palette);
    }
    if (dump888)
    {
      WriteRgb888Frame(dump888, frame, palette);
    }

    if (frame.durationMs > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(frame.durationMs));
    }
  }

  if (dumpTxt) fclose(dumpTxt);
  if (dump565) fclose(dump565);
  if (dump888) fclose(dump888);

  return 0;
}
