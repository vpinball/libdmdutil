#include <signal.h>

#include "DMDUtil/DMDUtil.h"
#include "DMDUtil/DMDServer.h"
#include "Logger.h"
#include "cargs.h"

using namespace std;

bool opt_verbose = false;
static volatile bool running = true;

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
    {.identifier = 'u',
     .access_letters = "u",
     .access_name = "pup-videos-path",
     .value_name = "VALUE",
     .description = "Fixed PupVideos path, overwriting paths transmitted by DMDUpdates (optional)"},
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

void DMDUTILCALLBACK LogCallback(DMDUtil_LogLevel logLevel, const char* format, va_list args)
{
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);
  fprintf(stderr, "%s\n", buffer);
}

// Signal Handler für graceful shutdown
void SignalHandler(int signum)
{
  if (signum == SIGTERM)
  {
    DMDUtil::Log(DMDUtil_LogLevel_INFO, "Received SIGTERM, shutting down...");
    running = false;
  }
}

int main(int argc, char* argv[])
{
  // Signal Handler registrieren
  signal(SIGTERM, SignalHandler);

  DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
  pConfig->SetDMDServer(false);  // This is the server. It must not connect to a different server!

  cag_option_context cag_context;
  bool opt_wait = false;
  bool opt_fixedAltColorPath = false;
  bool opt_fixedPupPath = false;

  cag_option_init(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    char identifier = cag_option_get_identifier(&cag_context);
    if (identifier == 'c')
    {
      pConfig->parseConfigFile(cag_option_get_value(&cag_context));
      if (opt_verbose) DMDUtil::Log(DMDUtil_LogLevel_INFO, "Loaded config file");
    }
    else if (identifier == 'o')
    {
      pConfig->SetAltColorPath(cag_option_get_value(&cag_context));
    }
    else if (identifier == 'u')
    {
      pConfig->SetPUPVideosPath(cag_option_get_value(&cag_context));
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

  DMDUtil::DMD* pDmd = new DMDUtil::DMD();

  while (true)
  {
    pDmd->FindDisplays();
    while (DMDUtil::DMD::IsFinding()) this_thread::sleep_for(chrono::milliseconds(100));

    if (pDmd->HasDisplay() || !opt_wait) break;
    this_thread::sleep_for(chrono::milliseconds(1000));
  }

  if (!pDmd->HasDisplay())
  {
    DMDUtil::Log(DMDUtil_LogLevel_ERROR, "No DMD displays found.");
    delete pDmd;
    return 2;
  }

  std::string altColorPath = DMDUtil::Config::GetInstance()->GetAltColorPath();
  std::string pupVideosPath = DMDUtil::Config::GetInstance()->GetPUPVideosPath();

  DMDUtil::DMDServer server(pDmd, !altColorPath.empty(), !pupVideosPath.empty());

  if (!server.Start(pConfig->GetDMDServerAddr(), pConfig->GetDMDServerPort()))
  {
    return 1;
  }

  while (running && server.IsRunning() && pDmd->HasDisplay())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  server.Stop();
  delete pDmd;
  return 0;
}
