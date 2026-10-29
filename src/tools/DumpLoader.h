#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

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

bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix);
std::string GetBaseName(const std::string& path);
std::string StripExtension(const std::string& name);
bool IsZipFile(const std::string& path);
bool LoadTxtDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames);
bool LoadRgb565DumpStream(std::istream& file, uint8_t outDepth, std::vector<Frame>& frames, bool strictMode = true);
bool LoadRgb565Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames);
bool LoadRgb888Dump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames);
void FinalizeFrameDurations(std::vector<Frame>& frames);
bool LoadRawDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames);
bool LoadZipDump(const std::string& path, uint8_t outDepth, std::vector<Frame>& frames);
