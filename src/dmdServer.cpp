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
      // At the moment the server only listens on localhost.
      // Therefore, we don't have to take care about litte vs. big endian and can use memcpy.
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
              // At the moment the server only listens on localhost.
              // Therefore, we don't have to take care about litte vs. big endian and can use the buffer as uint16_t as
              // it is.
              pDmd->UpdateRGB16Data((uint16_t*)buffer, pHeader->width, pHeader->height);
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pHeader->length)) == pHeader->length && threadId == currentThreadId)
            {
              pDmd->UpdateRGB24Data(buffer, pHeader->width, pHeader->height);
            }
            break;

          default:
            // Other modes aren't supported via network.
            break;
        }
      }
    }
  }

  // Clear the DMD by sending a black screen.
  // Fixed dimension of 128x32 should be OK for all devices.
  memset(buffer, 0, sizeof(DMDUtil::DMD::Update));
  pDmd->UpdateRGB24Data(buffer, 128, 32);

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

  cerr << "No DMD displays found." << endl;
  return -1;
}
