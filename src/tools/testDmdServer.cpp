#include "test.h"

int main(int argc, const char* argv[])
{
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);
  pConfig->SetDMDServer(true);

  DMDUtil::DMD* pDmd = new DMDUtil::DMD();

  pDmd->FindDisplays();

  run(pDmd);

  delete pDmd;

  return 0;
}
