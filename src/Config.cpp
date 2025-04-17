#include "DMDUtil/Config.h"

#include "ini.h"

namespace DMDUtil
{

Config* Config::m_pInstance = nullptr;

Config* Config::GetInstance()
{
  if (!m_pInstance) m_pInstance = new Config();

  return m_pInstance;
}

Config::Config()
{
  m_altColor = true;
  m_altColorPath.clear();
  m_pupCapture = false;
  m_pupVideosPath.clear();
  m_pupExactColorMatch = true;
  m_framesTimeout = 0;
  m_framesToSkip = 0;
  m_showNotColorizedFrames = false;
  m_dumpNotColorizedFrames = false;
  m_filterTransitionalFrames = false;
  m_zedmd = true;
  m_zedmdDevice.clear();
  m_zedmdDebug = false;
  m_zedmdBrightness = -1;
  m_pixelcade = true;
  m_pixelcadeDevice.clear();
  m_dmdServer = false;
  m_dmdServerAddr = "localhost";
  m_dmdServerPort = 6789;
  m_logLevel = DMDUtil_LogLevel_INFO;
  m_logCallback = nullptr;
  memset(&m_pupTriggerCallbackContext, 0, sizeof(m_pupTriggerCallbackContext));
}

void Config::parseConfigFile(const char* path)
{
  inih::INIReader r{path};

  SetDMDServerAddr(r.Get<std::string>("DMDServer", "Addr", "localhost").c_str());
  SetDMDServerPort(r.Get<int>("DMDServer", "Port", 6789));
  SetAltColor(r.Get<bool>("DMDServer", "AltColor", true));
  SetAltColorPath(r.Get<std::string>("DMDServer", "AltColorPath", "").c_str());
  SetPUPCapture(r.Get<bool>("DMDServer", "PUPCapture", false));
  SetPUPVideosPath(r.Get<std::string>("DMDServer", "PUPVideosPath", "").c_str());
  SetPUPExactColorMatch(r.Get<bool>("DMDServer", "PUPExactColorMatch", false));
  // ZeDMD
  SetZeDMD(r.Get<bool>("ZeDMD", "Enabled", true));
  SetZeDMDDevice(r.Get<std::string>("ZeDMD", "Device", "").c_str());
  SetZeDMDDebug(r.Get<bool>("ZeDMD", "Debug", false));
  SetZeDMDBrightness(r.Get<int>("ZeDMD", "Brightness", -1));
  // ZeDMD WiFi
  SetZeDMDWiFiEnabled(r.Get<bool>("ZeDMD-WiFi", "Enabled", false));
  SetZeDMDWiFiAddr(r.Get<std::string>("ZeDMD-WiFi", "WiFiAddr", "").c_str());
  // Pixelcade
  SetPixelcade(r.Get<bool>("Pixelcade", "Enabled", true));
  SetPixelcadeDevice(r.Get<std::string>("Pixelcade", "Device", "").c_str());
}

}  // namespace DMDUtil
