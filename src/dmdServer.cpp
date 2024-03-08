#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "cargs.h"
#include "sockpp/tcp_acceptor.h"

#define DMDSERVER_MAX_WIDTH 256
#define DMDSERVER_MAX_HEIGHT 64

using namespace std;

DMDUtil::DMD* pDmd;
uint32_t currentThreadId = 0;
std::vector<uint32_t> threads;

static struct cag_option options[] = {
    {.identifier = 'a',
     .access_letters = "a",
     .access_name = "addr",
     .value_name = "VALUE",
     .description = "IP address or host name (optional, default is 'localhost')"},
    {.identifier = 'p',
     .access_letters = "p",
     .access_name = "port",
     .value_name = "VALUE",
     .description = "Port (optional, default is '6789')"},
    {.identifier = 'w',
     .access_letters = "w",
     .access_name = "wait-for-displays",
     .value_name = NULL,
     .description = "Don't terminate if no displays are connected (optional, default is to terminate the server "
                    "process if no displays could be found)"},
    {.identifier = 'h', .access_letters = "h", .access_name = "help", .description = "Show help"}};

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

              if (data.width <= DMDSERVER_MAX_WIDTH && data.height <= DMDSERVER_MAX_HEIGHT)
              {
                pDmd->SetRomName(pHeader->name);
                pDmd->SetAltColorPath(pHeader->path);

                pDmd->QueueUpdate(data);
              }
            }
            break;

          case DMDUtil::DMD::Mode::RGB16:
            if ((n = sock.read_n(buffer, pHeader->length)) == pHeader->length && threadId == currentThreadId &&
                pHeader->width <= DMDSERVER_MAX_WIDTH && pHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              // At the moment the server only listens on localhost.
              // Therefore, we don't have to take care about litte vs. big endian and can use the buffer as uint16_t as
              // it is.
              pDmd->UpdateRGB16Data((uint16_t*)buffer, pHeader->width, pHeader->height);
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pHeader->length)) == pHeader->length && threadId == currentThreadId &&
                pHeader->width <= DMDSERVER_MAX_WIDTH && pHeader->height <= DMDSERVER_MAX_HEIGHT)
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

int main(int argc, char* argv[])
{
  uint32_t threadId = 0;
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetLogCallback(LogCallback);
  pConfig->SetDmdServer(false);  // This is the server. It must not connect to a different server!
  pConfig->SetDmdServerAddr("localhost");
  pConfig->SetDmdServerPort(6789);

  cag_option_context cag_context;
  bool opt_wait = false;
  const char* pPort = nullptr;

  cag_option_prepare(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    char identifier = cag_option_get(&cag_context);
    switch (identifier)
    {
      case 'a':
        pConfig->SetDmdServerAddr(cag_option_get_value(&cag_context));
        break;

      case 'p':
        pPort = cag_option_get_value(&cag_context);
        break;

      case 'w':
        opt_wait = true;
        break;

      case 'h':
        cout << "Usage: dmdserver [OPTION]..." << endl;
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
    }
  }

  if (pPort)
  {
    int port;
    std::stringstream ssPort(pPort);
    ssPort >> port;
    pConfig->SetDmdServerPort(port);
  }

  sockpp::initialize();

  sockpp::tcp_acceptor acc({pConfig->GetDmdServerAddr(), pConfig->GetDmdServerPort()});
  if (!acc)
  {
    cerr << "Error creating the acceptor: " << acc.last_error_str() << endl;
    return 1;
  }

  pDmd = new DMDUtil::DMD();

  while (true)
  {
    pDmd->FindDisplays();
    while (DMDUtil::DMD::IsFinding()) this_thread::sleep_for(chrono::milliseconds(100));

    if (pDmd->HasDisplay() || !opt_wait) break;

    this_thread::sleep_for(chrono::milliseconds(1000));
  }

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
