#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "DMDUtil/DMDUtil.h"
#include "Logger.h"
#include "cargs.h"
#include "ini.h"
#include "sockpp/tcp_acceptor.h"

#define DMDSERVER_MAX_WIDTH 256
#define DMDSERVER_MAX_HEIGHT 64

using namespace std;

DMDUtil::DMD* pDmd;
uint32_t currentThreadId = 0;
mutex threadMutex;
bool disconnectOtherClients = false;
vector<uint32_t> threads;
bool opt_verbose = false;
bool opt_fixedAltColorPath = false;

static struct cag_option options[] = {
    {.identifier = 'c',
     .access_letters = "c",
     .access_name = "config",
     .value_name = "VALUE",
     .description = "Config file (optional, default is no config file)"},
    {.identifier = 'o',
     .access_letters = "o",
     .access_name = "alt-color-path",
     .value_name = "VALUE",
     .description = "Fixed alt color path, overwriting paths transmitted by DMDUpdates (optional)"},
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
  // Disconnect others is only allowed once per client.
  bool handleDisconnectOthers = true;

  if (opt_verbose) DMDUtil::Log("%d: New DMD client %d connected", threadId, threadId);

  while (threadId == currentThreadId || !disconnectOtherClients)
  {
    n = sock.read_n(buffer, sizeof(DMDUtil::DMD::StreamHeader));
    // If the client disconnects or if a network error ocurres, exit the loop and terminate this thread.
    if (n <= 0) break;

    if (n == sizeof(DMDUtil::DMD::StreamHeader))
    {
      // At the moment the server only listens on localhost.
      // Therefore, we don't have to take care about litte vs. big endian and can use memcpy.
      memcpy(pStreamHeader, buffer, n);
      if (strcmp(pStreamHeader->header, "DMDStream") == 0 && pStreamHeader->version == 1)
      {
        if (opt_verbose)
        {
          DMDUtil::Log("%d: Received DMDStream header version %d for DMD mode %d", threadId, pStreamHeader->version,
                       pStreamHeader->mode);
          if (pStreamHeader->buffered) DMDUtil::Log("%d: Next data will be buffered", threadId);
        }

        // Only the current (most recent) thread is allowed to disconnect other clients.
        if (handleDisconnectOthers && threadId == currentThreadId && pStreamHeader->disconnectOthers)
        {
          disconnectOtherClients = true;
          handleDisconnectOthers = false;
          if (opt_verbose) DMDUtil::Log("%d: Other clients will be disconnected", threadId);
        }

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
                  DMDUtil::Log("%d: Received AltColor header: ROM '%s', AltColorPath '%s'", threadId,
                               altColorHeader.name, altColorHeader.path);
                DMDUtil::DMD::Update data;
                memcpy(&data, buffer, n);

                if (data.width <= DMDSERVER_MAX_WIDTH && data.height <= DMDSERVER_MAX_HEIGHT)
                {
                  pDmd->SetRomName(altColorHeader.name);
                  if (!opt_fixedAltColorPath) pDmd->SetAltColorPath(altColorHeader.path);

                  pDmd->QueueUpdate(data, pStreamHeader->buffered == 1);
                }
                else
                {
                  DMDUtil::Log("%d: TCP data package is missing or corrupted!", threadId);
                }
              }
              else
              {
                DMDUtil::Log("%d: AltColor header is missing!", threadId);
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
              pDmd->UpdateRGB16Data((uint16_t*)buffer, pStreamHeader->width, pStreamHeader->height,
                                    pStreamHeader->buffered == 1);
            }
            else
            {
              DMDUtil::Log("%d: TCP data package is missing or corrupted!", threadId);
            }
            break;

          case DMDUtil::DMD::Mode::RGB24:
            if ((n = sock.read_n(buffer, pStreamHeader->length)) == pStreamHeader->length &&
                threadId == currentThreadId && pStreamHeader->width <= DMDSERVER_MAX_WIDTH &&
                pStreamHeader->height <= DMDSERVER_MAX_HEIGHT)
            {
              pDmd->UpdateRGB24Data(buffer, pStreamHeader->width, pStreamHeader->height, pStreamHeader->buffered == 1);
            }
            else
            {
              DMDUtil::Log("%d: TCP data package is missing or corrupted!", threadId);
            }
            break;

          default:
            // Other modes aren't supported via network.
            break;
        }
      }
      else if (threadId == currentThreadId)
      {
        DMDUtil::Log("%d: Received unknown TCP package", threadId);
      }
    }
  }

  // Display a buffered frame or clear the display on disconnect of the current thread.
  if (threadId == currentThreadId && !pStreamHeader->buffered && !pDmd->QueueBuffer())
  {
    if (opt_verbose) DMDUtil::Log("%d: Clear screen on disconnect", threadId);
    // Clear the DMD by sending a black screen.
    // Fixed dimension of 128x32 should be OK for all devices.
    memset(buffer, 0, sizeof(DMDUtil::DMD::Update));
    pDmd->UpdateRGB24Data(buffer, 128, 32);
  }

  threadMutex.lock();
  threads.erase(remove(threads.begin(), threads.end(), threadId), threads.end());
  currentThreadId = (threads.size() >= 1) ? threads.back() : 0;
  if (threads.size() <= 1) disconnectOtherClients = false;
  threadMutex.unlock();

  if (opt_verbose) DMDUtil::Log("%d: DMD client %d disconnected", threadId, threadId);

  free(pStreamHeader);
}

int main(int argc, char* argv[])
{
  uint32_t threadId = 0;
  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetDMDServer(false);  // This is the server. It must not connect to a different server!

  cag_option_context cag_context;
  bool opt_wait = false;

  cag_option_prepare(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    char identifier = cag_option_get(&cag_context);
    if (identifier == 'c')
    {
      inih::INIReader r{cag_option_get_value(&cag_context)};
      pConfig->SetDMDServerAddr(r.Get<string>("DMDServer", "Addr", "localhost").c_str());
      pConfig->SetDMDServerPort(r.Get<int>("DMDServer", "Port", 6789));
      pConfig->SetAltColor(r.Get<bool>("DMDServer", "AltColor", true));
      pConfig->SetAltColorPath(r.Get<string>("DMDServer", "AltColorPath", "").c_str());
      // ZeDMD
      pConfig->SetZeDMD(r.Get<bool>("ZeDMD", "Enabled", true));
      pConfig->SetZeDMDDevice(r.Get<string>("ZeDMD", "Device", "").c_str());
      pConfig->SetZeDMDDebug(r.Get<bool>("ZeDMD", "Debug", false));
      pConfig->SetZeDMDRGBOrder(r.Get<int>("ZeDMD", "RGBOrder", -1));
      pConfig->SetZeDMDBrightness(r.Get<int>("ZeDMD", "Brightness", -1));
      pConfig->SetZeDMDSaveSettings(r.Get<bool>("ZeDMD", "SaveSettings", false));
      // Pixelcade
      pConfig->SetPixelcade(r.Get<bool>("Pixelcade", "Enabled", true));
      pConfig->SetPixelcadeDevice(r.Get<string>("Pixelcade", "Device", "").c_str());
      pConfig->SetPixelcadeMatrix(r.Get<int>("Pixelcade", "Matrix", -1));

      if (opt_verbose) DMDUtil::Log("Loaded config file");
    }
    else if (identifier == 'o')
    {
      pConfig->SetAltColorPath(cag_option_get_value(&cag_context));
    }
    else if (identifier == 'a')
    {
      pConfig->SetDMDServerAddr(cag_option_get_value(&cag_context));
    }
    else if (identifier == 'p')
    {
      std::stringstream ssPort(cag_option_get_value(&cag_context));
      in_port_t port;
      ssPort >> port;
      pConfig->SetDMDServerPort(port);
    }
    else if (identifier == 'w')
    {
      opt_wait = true;
    }
    else if (identifier == 'v')
    {
      opt_verbose = true;
      pConfig->SetLogCallback(LogCallback);
    }
    else if (identifier == 'l')
    {
      pConfig->SetLogCallback(LogCallback);
    }
    else if (identifier == 'h')
    {
      cout << "Usage: dmdserver [OPTION]..." << endl;
      cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
      return 0;
    }
  }

  sockpp::initialize();
  if (opt_verbose)
    DMDUtil::Log("Opening DMDServer, listining for TCP connections on %s:%d", pConfig->GetDMDServerAddr(),
                 pConfig->GetDMDServerPort());
  sockpp::tcp_acceptor acc({pConfig->GetDMDServerAddr(), (in_port_t)pConfig->GetDMDServerPort()});
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

  std::string altColorPath = DMDUtil::Config::GetInstance()->GetAltColorPath();
  if (!altColorPath.empty())
  {
    pDmd->SetAltColorPath(altColorPath.c_str());
    opt_fixedAltColorPath = true;
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
      threadMutex.lock();
      currentThreadId = ++threadId;
      threads.push_back(currentThreadId);
      threadMutex.unlock();
      // Create a thread and transfer the new stream to it.
      thread thr(run, std::move(sock), currentThreadId);
      thr.detach();
    }
  }

  DMDUtil::Log("No DMD displays found.");
  return 2;
}
