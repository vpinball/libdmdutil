#include "test.h"

int main(int argc, const char* argv[])
{
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);

  DMDUtil::DMD* pDmd = new DMDUtil::DMD();

  pDmd->ConnectDMDServer();

  run(pDmd);

  delete pDmd;

  return 0;
}
