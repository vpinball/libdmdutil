#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cargs.h"

namespace
{
struct DumpFrame
{
  uint64_t index = 0;
  uint64_t timestampMs = 0;
  uint64_t durationMs = 0;
  uint64_t width = 0;
  uint64_t height = 0;
  uint64_t hash = 0;
  bool hasOutput = true;
  bool hasHasOutputField = false;
  bool hasSerumFrameId = false;
  bool hasSerumFeatureFlags = false;
  uint64_t serumFrameId = 0;
  uint64_t serumFeatureFlags = 0;
};

static bool ParseUIntField(const std::string& object, const char* key, uint64_t& outValue)
{
  const size_t keyPos = object.find(key);
  if (keyPos == std::string::npos)
  {
    return false;
  }
  size_t pos = keyPos + std::strlen(key);
  while (pos < object.size() && (object[pos] == ' ' || object[pos] == '\t')) ++pos;
  size_t endPos = pos;
  while (endPos < object.size() && object[endPos] >= '0' && object[endPos] <= '9') ++endPos;
  if (endPos == pos)
  {
    return false;
  }
  outValue = strtoull(object.substr(pos, endPos - pos).c_str(), nullptr, 10);
  return true;
}

static bool ParseBoolField(const std::string& object, const char* key, bool& outValue)
{
  const size_t keyPos = object.find(key);
  if (keyPos == std::string::npos)
  {
    return false;
  }
  size_t pos = keyPos + std::strlen(key);
  while (pos < object.size() && (object[pos] == ' ' || object[pos] == '\t')) ++pos;
  if (object.compare(pos, 4, "true") == 0)
  {
    outValue = true;
    return true;
  }
  if (object.compare(pos, 5, "false") == 0)
  {
    outValue = false;
    return true;
  }
  return false;
}

static bool LoadDumpFrames(const std::string& path, std::vector<DumpFrame>& outFrames)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return false;
  }
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t pos = 0;
  while (true)
  {
    const size_t indexPos = content.find("\"index\":", pos);
    if (indexPos == std::string::npos)
    {
      break;
    }
    const size_t objStart = content.rfind('{', indexPos);
    const size_t objEnd = content.find('}', indexPos);
    if (objStart == std::string::npos || objEnd == std::string::npos || objEnd <= objStart)
    {
      return false;
    }

    const std::string object = content.substr(objStart, objEnd - objStart + 1);
    DumpFrame frame{};
    if (!ParseUIntField(object, "\"index\":", frame.index))
    {
      return false;
    }

    if (!ParseUIntField(object, "\"timestampMs\":", frame.timestampMs))
    {
      if (!ParseUIntField(object, "\"outputTimestampMs\":", frame.timestampMs))
      {
        ParseUIntField(object, "\"inputTimestampMs\":", frame.timestampMs);
      }
    }

    if (!ParseUIntField(object, "\"durationMs\":", frame.durationMs))
    {
      ParseUIntField(object, "\"inputDurationMs\":", frame.durationMs);
    }

    if (!ParseUIntField(object, "\"width\":", frame.width))
    {
      if (!ParseUIntField(object, "\"outputWidth\":", frame.width))
      {
        ParseUIntField(object, "\"inputWidth\":", frame.width);
      }
    }

    if (!ParseUIntField(object, "\"height\":", frame.height))
    {
      if (!ParseUIntField(object, "\"outputHeight\":", frame.height))
      {
        ParseUIntField(object, "\"inputHeight\":", frame.height);
      }
    }

    if (!ParseUIntField(object, "\"hashFNV1a64\":", frame.hash))
    {
      ParseUIntField(object, "\"outputHashFNV1a64\":", frame.hash);
    }

    frame.hasHasOutputField = ParseBoolField(object, "\"hasOutput\":", frame.hasOutput);
    frame.hasSerumFrameId = ParseUIntField(object, "\"serumFrameId\":", frame.serumFrameId);
    frame.hasSerumFeatureFlags = ParseUIntField(object, "\"serumFeatureFlags\":", frame.serumFeatureFlags);

    outFrames.push_back(frame);
    pos = objEnd + 1;
  }
  return true;
}
}  // namespace

static struct cag_option options[] = {
    {.identifier = 'e', .access_letters = "e", .access_name = "expected", .value_name = "FILE", .description = "Expected JSON dump"},
    {.identifier = 'a', .access_letters = "a", .access_name = "actual", .value_name = "FILE", .description = "Actual JSON dump"},
    {.identifier = 'm',
     .access_letters = "m",
     .access_name = "max-diffs",
     .value_name = "N",
     .description = "Maximum mismatches to print (default: 25)"},
    {.identifier = 'd',
     .access_name = "ignore-duration",
     .description = "Ignore durationMs differences"},
    {.identifier = 't',
     .access_name = "ignore-timestamp",
     .description = "Ignore timestampMs differences"},
    {.identifier = 'h', .access_letters = "h", .access_name = "help", .description = "Show help"}};

int main(int argc, char* argv[])
{
  const char* expectedPath = nullptr;
  const char* actualPath = nullptr;
  uint32_t maxDiffs = 25;
  bool ignoreDuration = false;
  bool ignoreTimestamp = false;

  cag_option_context cagContext;
  cag_option_init(&cagContext, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cagContext))
  {
    const char id = cag_option_get_identifier(&cagContext);
    switch (id)
    {
      case 'e':
        expectedPath = cag_option_get_value(&cagContext);
        break;
      case 'a':
        actualPath = cag_option_get_value(&cagContext);
        break;
      case 'm':
      {
        const char* valueStr = cag_option_get_value(&cagContext);
        if (valueStr)
        {
          int value = atoi(valueStr);
          if (value > 0)
          {
            maxDiffs = static_cast<uint32_t>(value);
          }
        }
        break;
      }
      case 'd':
        ignoreDuration = true;
        break;
      case 't':
        ignoreTimestamp = true;
        break;
      case 'h':
        std::cerr << "Usage: " << argv[0] << " --expected A.json --actual B.json [options]\n";
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
      default:
        break;
    }
  }

  if (!expectedPath || !actualPath)
  {
    std::cerr << "Error: --expected and --actual are required\n";
    return 2;
  }

  std::vector<DumpFrame> expectedFrames;
  std::vector<DumpFrame> actualFrames;
  if (!LoadDumpFrames(expectedPath, expectedFrames))
  {
    std::cerr << "Error: failed to parse expected JSON dump " << expectedPath << "\n";
    return 2;
  }
  if (!LoadDumpFrames(actualPath, actualFrames))
  {
    std::cerr << "Error: failed to parse actual JSON dump " << actualPath << "\n";
    return 2;
  }

  uint32_t diffCount = 0;
  const size_t maxCount = expectedFrames.size() > actualFrames.size() ? expectedFrames.size() : actualFrames.size();
  for (size_t i = 0; i < maxCount; ++i)
  {
    if (i >= expectedFrames.size())
    {
      if (diffCount < maxDiffs)
      {
        std::cout << "Diff[" << diffCount + 1 << "] extra actual frame at index " << i << "\n";
      }
      ++diffCount;
      continue;
    }
    if (i >= actualFrames.size())
    {
      if (diffCount < maxDiffs)
      {
        std::cout << "Diff[" << diffCount + 1 << "] missing actual frame at index " << i << "\n";
      }
      ++diffCount;
      continue;
    }

    const DumpFrame& e = expectedFrames[i];
    const DumpFrame& a = actualFrames[i];
    const bool compareHasOutput = e.hasHasOutputField || a.hasHasOutputField;
    const bool compareSerumFrameId = e.hasSerumFrameId || a.hasSerumFrameId;
    const bool compareSerumFeatureFlags = e.hasSerumFeatureFlags || a.hasSerumFeatureFlags;
    const bool mismatch = e.hash != a.hash || e.width != a.width || e.height != a.height ||
                          (compareHasOutput && e.hasOutput != a.hasOutput) ||
                          (compareSerumFrameId && e.serumFrameId != a.serumFrameId) ||
                          (compareSerumFeatureFlags && e.serumFeatureFlags != a.serumFeatureFlags) ||
                          (!ignoreDuration && e.durationMs != a.durationMs) ||
                          (!ignoreTimestamp && e.timestampMs != a.timestampMs);

    if (!mismatch)
    {
      continue;
    }

    if (diffCount < maxDiffs)
    {
      std::cout << "Diff[" << diffCount + 1 << "] frame " << i << ": "
                << "hash " << e.hash << " vs " << a.hash << ", "
                << "size " << e.width << "x" << e.height << " vs " << a.width << "x" << a.height;
      if (compareHasOutput)
      {
        std::cout << ", hasOutput " << (e.hasOutput ? "true" : "false") << " vs "
                  << (a.hasOutput ? "true" : "false");
      }
      if (compareSerumFrameId)
      {
        std::cout << ", serumFrameId " << e.serumFrameId << " vs " << a.serumFrameId;
      }
      if (compareSerumFeatureFlags)
      {
        std::cout << ", serumFeatureFlags " << e.serumFeatureFlags << " vs " << a.serumFeatureFlags;
      }
      if (!ignoreDuration)
      {
        std::cout << ", duration " << e.durationMs << " vs " << a.durationMs;
      }
      if (!ignoreTimestamp)
      {
        std::cout << ", timestamp " << e.timestampMs << " vs " << a.timestampMs;
      }
      std::cout << "\n";
    }
    ++diffCount;
  }

  std::cout << "Compared expected=" << expectedFrames.size() << " actual=" << actualFrames.size()
            << " mismatches=" << diffCount << "\n";
  return diffCount == 0 ? 0 : 1;
}
