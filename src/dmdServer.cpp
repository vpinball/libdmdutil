#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "Logger.h"
#include "cargs.h"
#include "sockpp/tcp_acceptor.h"

#define DMDSERVER_MAX_WIDTH 256
#define DMDSERVER_MAX_HEIGHT 64

using namespace std;

DMDUtil::DMD* pDmd;
uint32_t currentThreadId = 0;
std::vector<uint32_t> threads;
bool opt_verbose = false;

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
    {.identifier = 'l',
     .access_letters = "l",
     .access_name = "logging",
     .value_name = NULL,
     .description = "Enable logging to stderr (optional, default is no logging)"},
    {.identifier = 'v',
     .access_letters = "v",
     .access_name = "verbose-logging",
     .value_name = NULL,
     .description = "Enables verbose logging, includes normal logging (optional, default is no logging)"},
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
  DMDUtil::DMD::StreamHeader* pStreamHeader = (DMDUtil::DMD::StreamHeader*)malloc(sizeof(DMDUtil::DMD::StreamHeader));
  ssize_t n;

  while ((n = sock.read_n(buffer, sizeof(DMDUtil::DMD::StreamHeader))) > 0)
  {
    if (n == sizeof(DMDUtil::DMD::StreamHeader))
    {
      // At the moment the server only listens on localhost.
      // Therefore, we don't have to take care about litte vs. big endian and can use memcpy.
      memcpy(pStreamHeader, buffer, n);
      if (strcmp(pStreamHeader->header, "DMDStream") == 0 && pStreamHeader->version == 1)
      {
        if (opt_verbose)
          DMDUtil::Log("Received DMDStream header version %d for DMD mode %d", pStreamHeader->version,
                       pStreamHeader->mode);
        switch (pStreamHeader->mode)
        {
          case DMDUtil::DMD::Mode::Data:
            if ((n = sock.read_n(buffer, sizeof(DMDUtil::DMD::AltColorHeader))) ==
                    sizeof(DMDUtil::DMD::AltColorHeader) &&
                threadId == currentThreadId)
            {
              DMDUtil::DMD::AltColorHeader altColorHeader;
              memcpy(&altColorHeader, buffer, n);

              if (strcmp(altColorHeader.header, "AltColor") == 0 &&
                  (n = sock.read_n(buffer, sizeof(DMDUtil::DMD::Update))) == sizeof(DMDUtil::DMD::Update) &&
                  threadId == currentThreadId)
              {
                if (opt_verbose)
                  DMDUtil::Log("Received AltColor header: ROM '%s', AltColorPath '%s'", altColorHeader.name,
                               altColorHeader.path);
                DMDUtil::DMD::Update data;
                memcpy(&data, buffer, n);

                if (data.width <= DMDSERVER_MAX_WIDTH && data.height <= DMDSERVER_MAX_HEIGHT)
                {
                  pDmd->SetRomName(altColorHeader.name);
                  pDmd->SetAltColorPath(altColorHeader.path);

                  pDmd->QueueUpdate(data);
                }
                else
                {
                  DMDUtil::Log("TCP data package is missing or corrupted!");
                }
              }
              else
              {
                DMDUtil::Log("AltColor header is missing!");
              }
            }
            break;

          case DMDUtil::DMD::Mode::RGB16:
            if ((n = sock.read_n(buffer, pStreamHeader->length)) == pStreamHeader->length &&
                threadId == currentThreadId && pStreamHeader->width <= DMDSERVER_MAX_WIDTH &&
                pStreamHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              // At the moment the server only listens on localhost.
              // Therefore, we don't have to take care about litte vs. big endian and can use the buffer as uint16_t as
              // it is.
              pDmd->UpdateRGB16Data((uint16_t*)buffer, pStreamHeader->width, pStreamHeader->height);
            }
            else
            {
              DMDUtil::Log("TCP data package is missing or corrupted!");
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pStreamHeader->length)) == pStreamHeader->length &&
                threadId == currentThreadId && pStreamHeader->width <= DMDSERVER_MAX_WIDTH &&
                pStreamHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              pDmd->UpdateRGB24Data(buffer, pStreamHeader->width, pStreamHeader->height);
            }
            else
            {
              DMDUtil::Log("TCP data package is missing or corrupted!");
            }
            break;

          default:
            // Other modes aren't supported via network.
            break;
        }
      }
      else
      {
        DMDUtil::Log("Received unknown TCP package");
      }
    }
  }

  // Clear the DMD by sending a black screen.
  // Fixed dimension of 128x32 should be OK for all devices.
  memset(buffer, 0, sizeof(DMDUtil::DMD::Update));
  pDmd->UpdateRGB24Data(buffer, 128, 32);

  threads.erase(remove(threads.begin(), threads.end(), threadId), threads.end());
  currentThreadId = threads.back();

  free(pStreamHeader);
}

int main(int argc, char* argv[])
{
  uint32_t threadId = 0;
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetDmdServer(false);  // This is the server. It must not connect to a different server!

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

      case 'v':
        opt_verbose = true;
      case 'l':
        pConfig->SetLogCallback(LogCallback);
        break;

      case 'h':
        cout << "Usage: dmdserver [OPTION]..." << endl;
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
    }
  }

  if (pPort)
  {
    in_port_t port;
    std::stringstream ssPort(pPort);
    ssPort >> port;
    pConfig->SetDmdServerPort(port);
  }

  sockpp::initialize();
  if (opt_verbose)
    DMDUtil::Log("Opening DMDServer, listining for TCP connections on %s:%d", pConfig->GetDmdServerAddr(),
                 pConfig->GetDmdServerPort());
  sockpp::tcp_acceptor acc({pConfig->GetDmdServerAddr(), (in_port_t)pConfig->GetDmdServerPort()});
  if (!acc)
  {
    DMDUtil::Log("Error creating the DMDServer acceptor: %s", acc.last_error_str().c_str());
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
      DMDUtil::Log("Error accepting incoming connection: %s", acc.last_error_str().c_str());
    }
    else
    {
      if (opt_verbose) DMDUtil::Log("New DMD client connected");
      currentThreadId = threadId++;
      threads.push_back(currentThreadId);
      // Create a thread and transfer the new stream to it.
      thread thr(run, std::move(sock), currentThreadId);
      thr.detach();
    }
  }

  DMDUtil::Log("No DMD displays found.");
  return 2;
}
