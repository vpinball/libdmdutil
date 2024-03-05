#include <cstring>

#include <chrono>
#include <thread>

#include "DMDUtil/DMDUtil.h"

void DMDUTILCALLBACK LogCallback(const char* format, va_list args)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);
  printf("%s\n", buffer);
}

uint8_t* CreateImage(int width, int height, int depth)
{
  uint8_t* pImage = (uint8_t*)malloc(width * height);
  int pos = 0;
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x) pImage[pos++] = x % ((depth == 2) ? 4 : 16);
  }

  return pImage;
}

uint8_t* CreateImageRGB24(int width, int height)
{
  uint8_t* pImage = (uint8_t*)malloc(width * height * 3);
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      int index = (y * width + x) * 3;
      pImage[index++] = (uint8_t)(255 * x / width);
      pImage[index++] = (uint8_t)(255 * y / height);
      pImage[index] = (uint8_t)(128);
    }
  }

  return pImage;
}

void run(DMDUtil::DMD* pDmd) {
  int width = 128;
  int height = 32;

  printf("Rendering...\n");

  uint8_t* pImage2 = CreateImage(128, 32, 2);
  uint8_t* pImage4 = CreateImage(128, 32, 4);
  uint8_t* pImage24 = CreateImageRGB24(128, 32);
  uint16_t image16[128 * 32];
  for (int i = 0; i < 128 * 32; i++)
  {
    int pos = i * 3;
    uint32_t r = pImage24[pos];
    uint32_t g = pImage24[pos + 1];
    uint32_t b = pImage24[pos + 2];

    image16[i] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
  }

  DMDUtil::LevelDMD* pLevelDMD128_2;
  DMDUtil::LevelDMD* pLevelDMD128_4;
  DMDUtil::LevelDMD* pLevelDMD196_4;
  DMDUtil::RGB24DMD* pRGB24DMD128;
  DMDUtil::RGB24DMD* pRGB24DMD196;
  DMDUtil::ConsoleDMD* pConsoleDMD;

  int ms = 200;
  for (int i = 0; i < 4; i++)
  {
    if (i == 0) pConsoleDMD = pDmd->CreateConsoleDMD(false);
    if (i == 0) pLevelDMD128_2 = pDmd->CreateLevelDMD(128, 32, 2);
    if (i == 1) pDmd->DestroyConsoleDMD(pConsoleDMD);
    if (i == 1) pLevelDMD128_4 = pDmd->CreateLevelDMD(128, 32, 4);
    if (i == 2) pLevelDMD196_4 = pDmd->CreateLevelDMD(192, 64, 4);
    if (i == 3) pDmd->DestroyLevelDMD(pLevelDMD128_2);

    if (i == 1) pRGB24DMD128 = pDmd->CreateRGB24DMD(128, 32);
    if (i == 2) pRGB24DMD196 = pDmd->CreateRGB24DMD(192, 64);
    if (i == 3) pDmd->DestroyRGB24DMD(pRGB24DMD196);

    printf("Delay %dms\n", ms);

    pDmd->UpdateData(pImage2, 2, 128, 32, 255, 0, 0, "test2");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage2, 2, 128, 32, 0, 255, 0, "test2");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage2, 2, 128, 32, 0, 0, 255, "test2");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage2, 2, 128, 32, 255, 255, 255, "test2");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage4, 4, 128, 32, 255, 0, 0, "test4");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage4, 4, 128, 32, 0, 255, 0, "test4");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage4, 4, 128, 32, 0, 0, 255, "test4");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateData(pImage4, 4, 128, 32, 255, 255, 255, "test4");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 128, 32);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 2, 128, 32, 255, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 2, 128, 32, 0, 255, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 2, 128, 32, 0, 0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 2, 128, 32, 255, 255, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 4, 128, 32, 255, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 4, 128, 32, 0, 255, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 4, 128, 32, 0, 0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 4, 128, 32, 255, 255, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB24Data(pImage24, 128, 32);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    pDmd->UpdateRGB16Data(image16, 128, 32);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    ms -= 60;
  }

  FILE* fileptr;
  uint16_t size = width * height * 2;
  uint8_t* buffer = (uint8_t*)malloc(size * sizeof(uint8_t));
  uint16_t* rgb565 = (uint16_t*)malloc(size / 2 * sizeof(uint16_t));
  char filename[28];

  for (int i = 1; i <= 100; i++)
  {
    snprintf(filename, 28, "test/rgb565_%dx%d/%04d.raw", width, height, i);
    printf("Render raw: %s\n", filename);
    fileptr = fopen(filename, "rb");
    if (!fileptr) continue;
    fread(buffer, size, 1, fileptr);
    fclose(fileptr);

    memcpy(rgb565, buffer, size);
    pDmd->UpdateRGB16Data(rgb565, 128, 32);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }

  free(buffer);
  free(rgb565);

  printf("Finished rendering\n");

  free(pImage2);
  free(pImage4);
  free(pImage24);
}
