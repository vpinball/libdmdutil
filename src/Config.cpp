#include "DMDUtil/Config.h"

#include <cstring>

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
  m_zedmd = true;
  m_zedmdDevice.clear();
  m_zedmdDebug = false;
  m_zedmdRgbOrder = -1;
  m_zedmdBrightness = -1;
  m_zedmdSaveSettings = false;
  m_pixelcade = true;
  m_pixelcadeDevice.clear();
  m_pixelcadeMatrix = 0;
  m_dmdServer = false;
  m_dmdServerAddr = "localhost";
  m_dmdServerPort = 6789;
  m_logLevel = DMDUtil_LogLevel_INFO;
  m_logCallback = nullptr;
  memset(&m_pupTriggerCallbackContext, 0, sizeof(m_pupTriggerCallbackContext));
}

}  // namespace DMDUtil
