#include <chrono>
#include <cstring>
#include <thread>

#include "DMDUtil/DMDUtil.h"
#include "TcpServer.hpp"

DMDUtil::DMD* pDmd;

void DMDUTILCALLBACK LogCallback(const char* format, va_list args)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);
  fprintf(stderr, "%s\n", buffer);
}

void acceptClient(std::shared_ptr<CppSockets::TcpClient> client)
{
  DMDUtil::DMD::StreamHeader* pHeader;
  while (true)
  {
    auto cntHeader = client->receiveData(pHeader, sizeof(DMDUtil::DMD::StreamHeader));
    if (cntHeader == sizeof(DMDUtil::DMD::StreamHeader))
    {
      if (strcmp(pHeader->protocol, "DMDStream") == 0 && pHeader->version == 1)
      {
        switch (pHeader->mode)
        {
          case DMDUtil::DMD::Mode::Data:
            DMDUtil::DMD::Update* pData;
            auto cntData = client->receiveData(pData, sizeof(DMDUtil::DMD::Update));
            if (cntData == sizeof(DMDUtil::DMD::Update))
            {
              pDmd->QueueUpdate(pData);
            }
            break;
        }
      }
    }
  }
}

int main(int argc, const char* argv[])
{
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);

  pDmd = new DMDUtil::DMD();

  pDmd->FindDisplays();

  CppSockets::cppSocketsInit();
  CppSockets::TcpServer server(6789);
  server.acceptCallback = &acceptClient;
  server.startListening();

  while (DMDUtil::DMD::IsFinding()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (pDmd->HasDisplay())
    ;

  server.stopListening();
  server.close();

  return 0;
}
