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
  m_dumpFrames = false;
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
  m_localDisplaysActive = true;
  m_logLevel = DMDUtil_LogLevel_INFO;
  m_logCallback = nullptr;
  memset(&m_pupTriggerCallbackContext, 0, sizeof(m_pupTriggerCallbackContext));
}

void Config::parseConfigFile(const char* path)
{
  inih::INIReader r{path};

  // DMDServer
  try
  {
    SetDMDServerAddr(r.Get<std::string>("DMDServer", "Addr", "localhost").c_str());
  }
  catch (const std::exception&)
  {
    SetDMDServerAddr("localhost");
  }

  try
  {
    SetDMDServerPort(r.Get<int>("DMDServer", "Port", 6789));
  }
  catch (const std::exception&)
  {
    SetDMDServerPort(6789);
  }

  try
  {
    SetAltColor(r.Get<bool>("DMDServer", "AltColor", true));
  }
  catch (const std::exception&)
  {
    SetAltColor(true);
  }

  try
  {
    SetAltColorPath(r.Get<std::string>("DMDServer", "AltColorPath", "").c_str());
  }
  catch (const std::exception&)
  {
    SetAltColorPath("");
  }

  try
  {
    SetPUPCapture(r.Get<bool>("DMDServer", "PUPCapture", false));
  }
  catch (const std::exception&)
  {
    SetPUPCapture(false);
  }

  try
  {
    SetPUPVideosPath(r.Get<std::string>("DMDServer", "PUPVideosPath", "").c_str());
  }
  catch (const std::exception&)
  {
    SetPUPVideosPath("");
  }

  try
  {
    SetPUPExactColorMatch(r.Get<bool>("DMDServer", "PUPExactColorMatch", false));
  }
  catch (const std::exception&)
  {
    SetPUPExactColorMatch(false);
  }

  // ZeDMD
  try
  {
    SetZeDMD(r.Get<bool>("ZeDMD", "Enabled", true));
  }
  catch (const std::exception&)
  {
    SetZeDMD(true);
  }

  try
  {
    SetZeDMDDevice(r.Get<std::string>("ZeDMD", "Device", "").c_str());
  }
  catch (const std::exception&)
  {
    SetZeDMDDevice("");
  }

  try
  {
    SetZeDMDDebug(r.Get<bool>("ZeDMD", "Debug", false));
  }
  catch (const std::exception&)
  {
    SetZeDMDDebug(false);
  }

  try
  {
    SetZeDMDBrightness(r.Get<int>("ZeDMD", "Brightness", -1));
  }
  catch (const std::exception&)
  {
    SetZeDMDBrightness(-1);
  }

  // ZeDMD WiFi
  try
  {
    SetZeDMDWiFiEnabled(r.Get<bool>("ZeDMD-WiFi", "Enabled", false));
  }
  catch (const std::exception&)
  {
    SetZeDMDWiFiEnabled(false);
  }

  try
  {
    SetZeDMDWiFiAddr(r.Get<std::string>("ZeDMD-WiFi", "WiFiAddr", "").c_str());
  }
  catch (const std::exception&)
  {
    SetZeDMDWiFiAddr("");
  }

  try
  {
    SetZeDMDSpiEnabled(r.Get<bool>("ZeDMD-SPI", "Enabled", false));
  }
  catch (const std::exception&)
  {
    SetZeDMDSpiEnabled(false);
  }

  try
  {
    SetZeDMDWidth(r.Get<int>("ZeDMD-SPI", "Width", 128));
  }
  catch (const std::exception&)
  {
    SetZeDMDWidth(128);
  }

  try
  {
    SetZeDMDHeight(r.Get<int>("ZeDMD-SPI", "Height", 32));
  }
  catch (const std::exception&)
  {
    SetZeDMDHeight(32);
  }

  // Pixelcade
  try
  {
    SetPixelcade(r.Get<bool>("Pixelcade", "Enabled", true));
  }
  catch (const std::exception&)
  {
    SetPixelcade(true);
  }

  try
  {
    SetPixelcadeDevice(r.Get<std::string>("Pixelcade", "Device", "").c_str());
  }
  catch (const std::exception&)
  {
    SetPixelcadeDevice("");
  }

  // Serum
  try
  {
    SetIgnoreUnknownFramesTimeout(r.Get<int>("Serum", "IgnoreUnknownFramesTimeout", 0));
  }
  catch (const std::exception&)
  {
    SetIgnoreUnknownFramesTimeout(0);
  }

  try
  {
    SetMaximumUnknownFramesToSkip(r.Get<int>("Serum", "MaximumUnknownFramesToSkip", 0));
  }
  catch (const std::exception&)
  {
    SetMaximumUnknownFramesToSkip(0);
  }

  try
  {
    SetShowNotColorizedFrames(r.Get<bool>("Serum", "ShowNotColorizedFrames", false));
  }
  catch (const std::exception&)
  {
    SetShowNotColorizedFrames(false);
  }

  // Dump
  try
  {
    SetDumpNotColorizedFrames(r.Get<bool>("Dump", "DumpNotColorizedFrames", false));
  }
  catch (const std::exception&)
  {
    SetDumpNotColorizedFrames(false);
  }

  try
  {
    SetDumpFrames(r.Get<bool>("Dump", "DumpFrames", false));
  }
  catch (const std::exception&)
  {
    SetDumpFrames(false);
  }

  try
  {
    SetDumpPath(r.Get<std::string>("Dump", "DumpPath", "").c_str());
  }
  catch (const std::exception&)
  {
    SetDumpPath("");
  }

  try
  {
    SetFilterTransitionalFrames(r.Get<bool>("Dump", "FilterTransitionalFrames", false));
  }
  catch (const std::exception&)
  {
    SetFilterTransitionalFrames(false);
  }
}

}  // namespace DMDUtil
