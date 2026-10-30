#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"

namespace
{

int g_failures = 0;

void Check(bool condition, const char* what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    g_failures++;
  }
  else
  {
    printf("ok:   %s\n", what);
  }
}

bool WaitFor(const std::function<bool()>& predicate, int timeoutMs = 2000)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

bool WaitForData(DMDUtil::RGB24DMD* pDisplay, std::vector<uint8_t>& out, int timeoutMs = 2000)
{
  return WaitFor(
      [&]()
      {
        uint8_t* pData = pDisplay->GetData();
        if (!pData) return false;
        out.assign(pData, pData + pDisplay->GetLength());
        return true;
      },
      timeoutMs);
}

bool PixelIs(const std::vector<uint8_t>& rgb, int x, int y, int width, uint8_t r, uint8_t g, uint8_t b)
{
  const size_t pos = ((size_t)y * width + x) * 3;
  return rgb[pos] == r && rgb[pos + 1] == g && rgb[pos + 2] == b;
}

void TestConfig()
{
  const char* path = "dmdutil_unit_test.ini";
  {
    std::ofstream ini(path);
    ini << "[ZeDMD]\nEnabled = 0\nBrightness = 7\n[DMDExtStream]\nEnabled = 1\nUrl = ws://host:81/x\nRetry = 1\n"
           "RetryInterval = 9\n[Serum]\nExcludePinDMD3 = 1\n";
  }
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->parseConfigFile(path);
  remove(path);

  Check(!pConfig->IsZeDMD(), "config: ZeDMD disabled");
  Check(pConfig->GetZeDMDBrightness() == 7, "config: ZeDMD brightness");
#ifdef DMDUTIL_ENABLE_DMDEXTSTREAM
  Check(pConfig->IsDMDExtStream(), "config: DMDExtStream enabled");
  Check(strcmp(pConfig->GetDMDExtStreamUrl(), "ws://host:81/x") == 0, "config: DMDExtStream url");
  Check(pConfig->IsDMDExtStreamRetry() && pConfig->GetDMDExtStreamRetryInterval() == 9, "config: retry settings");
#else
  Check(!pConfig->IsDMDExtStream(), "config: DMDExtStream section ignored when compiled out");
#endif
#if defined(DMDUTIL_ENABLE_PINDMD3) && (defined(DMDUTIL_ENABLE_SERUM) || defined(DMDUTIL_ENABLE_VNI))
  Check(pConfig->IsExcludeColorizedFramesForPinDMD3(), "config: ExcludePinDMD3");
#else
  Check(!pConfig->IsExcludeColorizedFramesForPinDMD3(), "config: ExcludePinDMD3 ignored when compiled out");
#endif
  Check(pConfig->IsPixelcade(), "config: untouched defaults survive");

  pConfig->SetDMDExtStream(false);
  pConfig->SetZeDMD(false);
}

void TestRendering()
{
  DMDUtil::DMD dmd;
  DMDUtil::RGB24DMD* pRgb = dmd.CreateRGB24DMD(128, 32);
  DMDUtil::LevelDMD* pLevel = dmd.CreateLevelDMD(128, 32, false);
  std::vector<uint8_t> rgb;

  std::vector<uint8_t> frame2(128 * 32);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 128; x++) frame2[y * 128 + x] = (uint8_t)(x % 4);

  dmd.UpdateData(frame2.data(), 2, 128, 32, 255, 0, 0);
  Check(WaitForData(pRgb, rgb), "2-bit frame reaches RGB24DMD");
  Check(PixelIs(rgb, 3, 0, 128, 255, 0, 0), "2-bit level 3 renders as full tint");
  Check(PixelIs(rgb, 0, 0, 128, 0, 0, 0), "2-bit level 0 renders black");

  Check(WaitFor([&]() { return pLevel->GetData() != nullptr; }), "2-bit frame reaches LevelDMD");

  dmd.UpdateData(frame2.data(), 2, 128, 32, 0, 0, 255);
  Check(WaitForData(pRgb, rgb), "tint change re-renders identical pixels");
  Check(PixelIs(rgb, 3, 0, 128, 0, 0, 255), "new tint applied");

  std::vector<uint8_t> frame24(128 * 32 * 3);
  for (size_t i = 0; i < 128 * 32; i++)
  {
    frame24[i * 3] = (uint8_t)(i & 0xff);
    frame24[i * 3 + 1] = (uint8_t)((i >> 8) & 0xff);
    frame24[i * 3 + 2] = 77;
  }
  dmd.UpdateRGB24Data(frame24.data(), 128, 32);
  Check(WaitForData(pRgb, rgb), "RGB24 frame reaches RGB24DMD");
  Check(rgb == frame24, "RGB24 frame passes through unchanged");

  std::vector<uint16_t> frame16(128 * 32, 0xF800);
  frame16[5] = 0x07E0;
  dmd.UpdateRGB16Data(frame16.data(), 128, 32);
  Check(WaitForData(pRgb, rgb), "RGB16 frame reaches RGB24DMD");
  Check(PixelIs(rgb, 0, 0, 128, 255, 0, 0) && PixelIs(rgb, 5, 0, 128, 0, 255, 0), "RGB16 expands to full range");

  std::vector<uint8_t> frame16rows(128 * 16);
  for (int i = 0; i < 128 * 16; i++) frame16rows[i] = 3;
  dmd.UpdateData(frame16rows.data(), 2, 128, 16, 0, 255, 0);
  Check(WaitForData(pRgb, rgb), "128x16 frame reaches RGB24DMD");
  Check(PixelIs(rgb, 10, 0, 128, 0, 0, 0) && PixelIs(rgb, 10, 16, 128, 0, 255, 0), "128x16 frame is centered");

  for (int i = 0; i < 40; i++)
  {
    dmd.UpdateData(frame2.data(), 2, 128, 32, (uint8_t)(i * 6), (uint8_t)(255 - i * 6), 0);
  }
  dmd.UpdateData(frame2.data(), 2, 128, 32, 255, 255, 255);
  Check(WaitFor(
            [&]()
            {
              uint8_t* pData = pRgb->GetData();
              if (!pData) return false;
              rgb.assign(pData, pData + pRgb->GetLength());
              return PixelIs(rgb, 3, 0, 128, 255, 255, 255);
            }),
        "burst of frames ends on the last frame");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Check(pRgb->GetData() == nullptr, "nothing renders after the last frame");

  uint16_t segments[128] = {0};
  for (int i = 0; i < 32; i++) segments[i] = 0x7F;
  dmd.UpdateAlphaNumericData(DMDUtil::AlphaNumericLayout::__2x16Alpha, segments, nullptr, 255, 128, 0);
  Check(WaitForData(pRgb, rgb), "alphanumeric frame reaches RGB24DMD");
  bool lit = false;
  for (size_t i = 0; i < rgb.size() && !lit; i++) lit = rgb[i] != 0;
  Check(lit, "alphanumeric frame has lit segments");

  dmd.DestroyLevelDMD(pLevel);
  dmd.DestroyRGB24DMD(pRgb);
}

}  // namespace

int main()
{
  DMDUtil::Config::GetInstance()->SetLogLevel(DMDUtil_LogLevel_ERROR);
  TestConfig();
  TestRendering();
  printf("%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
