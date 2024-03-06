#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "sockpp/tcp_acceptor.h"

using namespace std;

DMDUtil::DMD* pDmd;
uint32_t currentThreadId = 0;
std::vector<uint32_t> threads;

void DMDUTILCALLBACK LogCallback(const char* format, va_list args)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);
  fprintf(stderr, "%s\n", buffer);
}

void run(sockpp::tcp_socket sock, uint32_t threadId)
{
  uint8_t buffer[sizeof(DMDUtil::DMD::Update)];
  DMDUtil::DMD::StreamHeader* pHeader = (DMDUtil::DMD::StreamHeader*)malloc(sizeof(DMDUtil::DMD::StreamHeader));
  ssize_t n;

  while ((n = sock.read_n(buffer, sizeof(DMDUtil::DMD::StreamHeader))) > 0)
  {
    if (n == sizeof(DMDUtil::DMD::StreamHeader))
    {
      memcpy(pHeader, buffer, n);
      if (strcmp(pHeader->protocol, "DMDStream") == 0 && pHeader->version == 1)
      {
        switch (pHeader->mode)
        {
          case DMDUtil::DMD::Mode::Data:
            if ((n = sock.read_n(buffer, sizeof(DMDUtil::DMD::Update))) == sizeof(DMDUtil::DMD::Update) &&
                threadId == currentThreadId)
            {
              DMDUtil::DMD::Update data;
              memcpy(&data, buffer, n);
              pDmd->QueueUpdate(data);
            }
            break;

          case DMDUtil::DMD::Mode::RGB16:
            if ((n = sock.read_n(buffer, pHeader->length)) == pHeader->length && threadId == currentThreadId)
            {
              pDmd->UpdateRGB16Data((uint16_t*)buffer, pHeader->width, pHeader->height);
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pHeader->length)) == pHeader->length && threadId == currentThreadId)
            {
              pDmd->UpdateRGB24Data(buffer, pHeader->width, pHeader->height);
            }
            break;
        }
      }
    }
  }

  threads.erase(remove(threads.begin(), threads.end(), threadId), threads.end());
  currentThreadId = threads.back();

  free(pHeader);
}

int main(int argc, const char* argv[])
{
  uint32_t threadId = 0;

  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);

  pDmd = new DMDUtil::DMD();

  pDmd->FindDisplays();

  sockpp::initialize();

  sockpp::tcp_acceptor acc({"localhost", 6789});
  if (!acc)
  {
    cerr << "Error creating the acceptor: " << acc.last_error_str() << endl;
    return 1;
  }

  while (DMDUtil::DMD::IsFinding()) this_thread::sleep_for(chrono::milliseconds(100));

  while (pDmd->HasDisplay())
  {
    sockpp::inet_address peer;

    // Accept a new client connection
    sockpp::tcp_socket sock = acc.accept(&peer);

    if (!sock)
    {
      cerr << "Error accepting incoming connection: " << acc.last_error_str() << endl;
    }
    else
    {
      currentThreadId = threadId++;
      threads.push_back(currentThreadId);
      // Create a thread and transfer the new stream to it.
      thread thr(run, std::move(sock), currentThreadId);
      thr.detach();
    }
  }

  return 0;
}
