#include "DMDUtil/Config.h"

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
  m_dmdServer = false;
  m_dmdServerAddr.clear();
  m_dmdServerPort = 6789;
  m_logCallback = nullptr;
}

}  // namespace DMDUtil
