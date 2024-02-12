#include <chrono>
#include <thread>

#include "DMDUtil/DMDUtil.h"
#include "DMDUtil/VirtualDMD.h"

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

int main(int argc, const char* argv[])
{
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);

  int width = 128;
  int height = 32;

  DMDUtil::DMD* pDmd = new DMDUtil::DMD();

  printf("Finding displays...\n");

  while (DMDUtil::DMD::IsFinding()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (!pDmd->HasDisplay())
  {
    printf("No displays to render.\n");
    delete pDmd;
    return 1;
  }

  printf("Rendering...\n");

  uint8_t* pImage2 = CreateImage(128, 32, 2);
  uint8_t* pImage4 = CreateImage(128, 32, 4);
  uint8_t* pImage24 = CreateImageRGB24(128, 32);

  DMDUtil::VirtualDMD *pVirtualDMD128;
  DMDUtil::VirtualDMD *pVirtualDMD196;

  for (int i = 0; i < 4; i++)
  {
    if (i == 1) pVirtualDMD128 = pDmd->CreateVirtualDMD(128, 32);
    if (i == 2) pVirtualDMD196 = pDmd->CreateVirtualDMD(192, 64);
    if (i == 3) pDmd->DestroyVirtualDMD(pVirtualDMD196);

    pDmd->UpdateData(pImage2, 2, 128, 32, 255, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage2, 2, 128, 32, 0, 255, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage2, 2, 128, 32, 0, 0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage2, 2, 128, 32, 255, 255, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage4, 4, 128, 32, 255, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage4, 4, 128, 32, 0, 255, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage4, 4, 128, 32, 0, 0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateData(pImage4, 4, 128, 32, 255, 255, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pDmd->UpdateRGB24Data(pImage24, 128, 32, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  printf("Finished rendering\n");

  free(pImage2);
  free(pImage4);
  free(pImage24);

  delete pDmd;

  return 0;
}
