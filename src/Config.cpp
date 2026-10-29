#include "DMDUtil/Config.h"

#include "ini.h"

namespace DMDUtil
{

namespace
{

template <typename T>
T ReadOr(inih::INIReader& r, const char* section, const char* key, const T& defaultValue)
{
  try
  {
    return r.Get<T>(section, key, T(defaultValue));
  }
  catch (const std::exception&)
  {
    return defaultValue;
  }
}

}  // namespace

Config* Config::m_pInstance = nullptr;

Config* Config::GetInstance()
{
  if (!m_pInstance) m_pInstance = new Config();

  return m_pInstance;
}

void Config::SetInstance(Config* pInstance)
{
  if (m_pInstance == pInstance) return;
  delete m_pInstance;
  m_pInstance = pInstance;
}

Config::Config()
{
  m_altColor = true;
  m_altColorPath.clear();
  m_pupCapture = false;
  m_serumPupTriggers = false;
  m_vniKey.clear();
  m_pupVideosPath.clear();
  m_pupExactColorMatch = true;
  m_framesTimeout = 0;
  m_framesToSkip = 0;
  m_showNotColorizedFrames = false;
  m_excludeColorizedFramesForZeDMD = false;
  m_excludeColorizedFramesForRGB24DMD = false;
  m_excludeColorizedFramesForPIN2DMD = false;
  m_excludeColorizedFramesForPixelcade = false;
  m_excludeColorizedFramesForPinDMD3 = false;
  m_excludeColorizedFramesForDMDExtStream = false;
  m_dumpNotColorizedFrames = false;
  m_dumpFrames = false;
  m_dumpZip = false;
  m_filterTransitionalFrames = false;
  m_roundedCorners = 0;
  m_zedmd = true;
  m_zedmdDevice.clear();
  m_zedmdDebug = false;
  m_zedmdBrightness = -1;
  m_zedmdWiFiEnabled = false;
  m_zedmdWiFiAddr.clear();
  m_zedmdSpiEnabled = false;
  m_zedmdSpiSpeed = 72000000;
  m_zedmdSpiFramePause = 2;
  m_zedmdWidth = 128;
  m_zedmdHeight = 32;
  m_pixelcade = true;
  m_pixelcadeDevice.clear();
  m_PIN2DMD = true;
  m_pinDMD3 = true;
  m_pinDMD3Device.clear();
  m_dmdExtStream = false;
  m_dmdExtStreamUrl = "ws://127.0.0.1/dmd";
  m_dmdExtStreamRetry = false;
  m_dmdExtStreamRetryInterval = 5;
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
  SetDMDServerAddr(ReadOr<std::string>(r, "DMDServer", "Addr", "localhost").c_str());
  SetDMDServerPort(ReadOr<int>(r, "DMDServer", "Port", 6789));
  SetAltColor(ReadOr<bool>(r, "DMDServer", "AltColor", true));
  SetAltColorPath(ReadOr<std::string>(r, "DMDServer", "AltColorPath", "").c_str());
  SetPUPCapture(ReadOr<bool>(r, "DMDServer", "PUPCapture", false));
  SetSerumPUPTriggers(ReadOr<bool>(r, "DMDServer", "SerumPUPTriggers", false));
  SetPUPVideosPath(ReadOr<std::string>(r, "DMDServer", "PUPVideosPath", "").c_str());
  SetPUPExactColorMatch(ReadOr<bool>(r, "DMDServer", "PUPExactColorMatch", false));

  // ZeDMD
  SetZeDMD(ReadOr<bool>(r, "ZeDMD", "Enabled", true));
  SetZeDMDDevice(ReadOr<std::string>(r, "ZeDMD", "Device", "").c_str());
  SetZeDMDDebug(ReadOr<bool>(r, "ZeDMD", "Debug", false));
  SetZeDMDBrightness(ReadOr<int>(r, "ZeDMD", "Brightness", -1));

  // ZeDMD WiFi
  SetZeDMDWiFiEnabled(ReadOr<bool>(r, "ZeDMD-WiFi", "Enabled", false));
  SetZeDMDWiFiAddr(ReadOr<std::string>(r, "ZeDMD-WiFi", "WiFiAddr", "").c_str());
  SetZeDMDSpiEnabled(ReadOr<bool>(r, "ZeDMD-SPI", "Enabled", false));
  SetZeDMDSpiSpeed(ReadOr<int>(r, "ZeDMD-SPI", "Speed", 72000000));
  SetZeDMDSpiFramePause(ReadOr<int>(r, "ZeDMD-SPI", "FramePause", 2));
  SetZeDMDWidth(ReadOr<int>(r, "ZeDMD-SPI", "Width", 128));
  SetZeDMDHeight(ReadOr<int>(r, "ZeDMD-SPI", "Height", 32));

#ifdef DMDUTIL_ENABLE_PIXELCADE
  // Pixelcade
  SetPixelcade(ReadOr<bool>(r, "Pixelcade", "Enabled", true));
  SetPixelcadeDevice(ReadOr<std::string>(r, "Pixelcade", "Device", "").c_str());
#endif

#ifdef DMDUTIL_ENABLE_PIN2DMD
  // PIN2DMD
  SetPIN2DMD(ReadOr<bool>(r, "PIN2DMD", "Enabled", true));
#endif

#ifdef DMDUTIL_ENABLE_PINDMD3
  // PinDMD3
  SetPinDMD3(ReadOr<bool>(r, "PinDMD3", "Enabled", true));
  SetPinDMD3Device(ReadOr<std::string>(r, "PinDMD3", "Device", "").c_str());
#endif

#ifdef DMDUTIL_ENABLE_DMDEXTSTREAM
  // DMDExtStream
  SetDMDExtStream(ReadOr<bool>(r, "DMDExtStream", "Enabled", false));
  SetDMDExtStreamUrl(ReadOr<std::string>(r, "DMDExtStream", "Url", "ws://127.0.0.1/dmd").c_str());
  SetDMDExtStreamRetry(ReadOr<bool>(r, "DMDExtStream", "Retry", false));
  SetDMDExtStreamRetryInterval(ReadOr<int>(r, "DMDExtStream", "RetryInterval", 5));
#endif

  // Serum
  SetIgnoreUnknownFramesTimeout(ReadOr<int>(r, "Serum", "IgnoreUnknownFramesTimeout", 0));
  SetMaximumUnknownFramesToSkip(ReadOr<int>(r, "Serum", "MaximumUnknownFramesToSkip", 0));
  SetShowNotColorizedFrames(ReadOr<bool>(r, "Serum", "ShowNotColorizedFrames", false));
  SetExcludeColorizedFramesForZeDMD(ReadOr<bool>(r, "Serum", "ExcludeZeDMD", false));
  SetExcludeColorizedFramesForRGB24DMD(ReadOr<bool>(r, "Serum", "ExcludeRGB24DMD", false));
#ifdef DMDUTIL_ENABLE_PIN2DMD
  SetExcludeColorizedFramesForPIN2DMD(ReadOr<bool>(r, "Serum", "ExcludePIN2DMD", false));
#endif
#ifdef DMDUTIL_ENABLE_PIXELCADE
  SetExcludeColorizedFramesForPixelcade(ReadOr<bool>(r, "Serum", "ExcludePixelcade", false));
#endif
#ifdef DMDUTIL_ENABLE_PINDMD3
  SetExcludeColorizedFramesForPinDMD3(ReadOr<bool>(r, "Serum", "ExcludePinDMD3", false));
#endif
#ifdef DMDUTIL_ENABLE_DMDEXTSTREAM
  SetExcludeColorizedFramesForDMDExtStream(ReadOr<bool>(r, "Serum", "ExcludeDMDExtStream", false));
#endif

  // VNI
  SetVniKey(ReadOr<std::string>(r, "VNI", "Key", "").c_str());

  // Dump
  SetDumpNotColorizedFrames(ReadOr<bool>(r, "Dump", "DumpNotColorizedFrames", false));
  SetDumpFrames(ReadOr<bool>(r, "Dump", "DumpFrames", false));
  SetDumpPath(ReadOr<std::string>(r, "Dump", "DumpPath", "").c_str());
  SetFilterTransitionalFrames(ReadOr<bool>(r, "Dump", "FilterTransitionalFrames", false));

  // OutputFilters
  SetRoundedCorners(ReadOr<int>(r, "OutputFilters", "RoundedCorners", 0));
}

}  // namespace DMDUtil
