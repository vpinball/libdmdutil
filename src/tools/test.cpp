#include "test.h"

int main(int argc, const char* argv[])
{
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);

  DMDUtil::DMD* pDmd = new DMDUtil::DMD();

  printf("Finding displays...\n");

  pDmd->FindDisplays();
  while (DMDUtil::DMD::IsFinding()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (!pDmd->HasDisplay())
  {
    printf("No hardware displays to render.\n");
  }

  pDmd->DumpDMDTxt();
  pDmd->DumpDMDRaw();

  run(pDmd);

  delete pDmd;

  return 0;
}
