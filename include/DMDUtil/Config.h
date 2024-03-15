#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdarg>
#include <string>

typedef void(DMDUTILCALLBACK* DMDUtil_LogCallback)(const char* format, va_list args);

namespace DMDUtil
{

class DMDUTILAPI Config
{
 public:
  static Config* GetInstance();
  bool IsAltColor() const { return m_altColor; }
  void SetAltColor(bool altColor) { m_altColor = altColor; }
  void SetAltColorPath(const char* path) { m_altColorPath = path; }
  const char* GetAltColorPath() const { return m_altColorPath.c_str(); }
  void SetIgnoreUnknownFramesTimeout(int framesTimeout) { m_framesTimeout = framesTimeout; }
  void SetMaximumUnknownFramesToSkip(int framesToSkip) { m_framesToSkip = framesToSkip; }
  int GetIgnoreUnknownFramesTimeout() { return m_framesTimeout; }
  int GetMaximumUnknownFramesToSkip() { return m_framesToSkip; }
  bool IsZeDMD() const { return m_zedmd; }
  void SetZeDMD(bool zedmd) { m_zedmd = zedmd; }
  const char* GetZeDMDDevice() const { return m_zedmdDevice.c_str(); }
  void SetZeDMDDevice(const char* port) { m_zedmdDevice = port; }
  bool IsZeDMDDebug() const { return m_zedmdDebug; }
  void SetZeDMDDebug(bool debug) { m_zedmdDebug = debug; }
  int GetZeDMDRGBOrder() const { return m_zedmdRgbOrder; }
  void SetZeDMDRGBOrder(int rgbOrder) { m_zedmdRgbOrder = rgbOrder; }
  int GetZeDMDBrightness() const { return m_zedmdBrightness; }
  void SetZeDMDBrightness(int brightness) { m_zedmdBrightness = brightness; }
  bool IsZeDMDSaveSettings() const { return m_zedmdSaveSettings; }
  void SetZeDMDSaveSettings(bool saveSettings) { m_zedmdSaveSettings = saveSettings; }
  bool IsPixelcade() const { return m_pixelcade; }
  void SetPixelcade(bool pixelcade) { m_pixelcade = pixelcade; }
  void SetPixelcadeDevice(const char* port) { m_pixelcadeDevice = port; }
  const char* GetPixelcadeDevice() const { return m_pixelcadeDevice.c_str(); }
  int GetPixelcadeMatrix() const { return m_pixelcadeMatrix; }
  void SetPixelcadeMatrix(int matrix) { m_pixelcadeMatrix = matrix; }
  void SetDMDServer(bool dmdServer) { m_dmdServer = dmdServer; }
  bool IsDmdServer() { return m_dmdServer; }
  void SetDMDServerAddr(const char* addr) { m_dmdServerAddr = addr; }
  const char* GetDMDServerAddr() const { return m_dmdServerAddr.c_str(); }
  void SetDMDServerPort(int port) { m_dmdServerPort = port; }
  int GetDMDServerPort() const { return m_dmdServerPort; }
  DMDUtil_LogCallback GetLogCallback() const { return m_logCallback; }
  void SetLogCallback(DMDUtil_LogCallback callback) { m_logCallback = callback; }

 private:
  Config();
  ~Config() {}

  static Config* m_pInstance;
  bool m_altColor;
  std::string m_altColorPath;
  int m_framesTimeout;
  int m_framesToSkip;
  bool m_zedmd;
  std::string m_zedmdDevice;
  bool m_zedmdDebug;
  int m_zedmdRgbOrder;
  int m_zedmdBrightness;
  bool m_zedmdSaveSettings;
  bool m_dmdServer;
  std::string m_dmdServerAddr;
  int m_dmdServerPort;
  bool m_pixelcade;
  std::string m_pixelcadeDevice;
  int m_pixelcadeMatrix;
  DMDUtil_LogCallback m_logCallback;
};

}  // namespace DMDUtil
