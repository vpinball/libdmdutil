#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdarg>
#include <cstdint>
#include <string>

typedef enum
{
  DMDUtil_LogLevel_DEBUG = 0,
  DMDUtil_LogLevel_INFO = 1,
  DMDUtil_LogLevel_ERROR = 2
} DMDUtil_LogLevel;

typedef void(DMDUTILCALLBACK* DMDUtil_LogCallback)(DMDUtil_LogLevel logLevel, const char* format, va_list args);

typedef void(DMDUTILCALLBACK* DMDUtil_PUPTriggerCallback)(uint16_t id, void* userData);

struct DMDUtil_PUPTriggerCallbackContext
{
  DMDUtil_PUPTriggerCallback callback;
  void* pUserData;
};

namespace DMDUtil
{

class DMDUTILAPI Config
{
 public:
  static Config* GetInstance();
  void parseConfigFile(const char* path);
  bool IsAltColor() const { return m_altColor; }
  void SetAltColor(bool altColor) { m_altColor = altColor; }
  void SetAltColorPath(const char* path) { m_altColorPath = path; }
  const char* GetAltColorPath() const { return m_altColorPath.c_str(); }
  bool IsPUPCapture() const { return m_pupCapture; }
  void SetPUPCapture(bool pupCapture) { m_pupCapture = pupCapture; }
  void SetPUPVideosPath(const char* path) { m_pupVideosPath = path; }
  const char* GetPUPVideosPath() const { return m_pupVideosPath.c_str(); }
  bool IsPUPExactColorMatch() const { return m_pupExactColorMatch; }
  void SetPUPExactColorMatch(bool exactColorMatch) { m_pupExactColorMatch = exactColorMatch; }
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
  int GetZeDMDBrightness() const { return m_zedmdBrightness; }
  void SetZeDMDBrightness(int brightness) { m_zedmdBrightness = brightness; }
  bool IsZeDMDWiFiEnabled() const { return m_zedmdWiFiEnabled; }
  void SetZeDMDWiFiEnabled(bool WiFiEnabled) { m_zedmdWiFiEnabled = WiFiEnabled; }
  const char* GetZeDMDWiFiAddr() const { return m_zedmdWiFiAddr.c_str(); }
  void SetZeDMDWiFiAddr(const char* ipaddr) { m_zedmdWiFiAddr = ipaddr; }
  bool IsPixelcade() const { return m_pixelcade; }
  void SetPixelcade(bool pixelcade) { m_pixelcade = pixelcade; }
  void SetPixelcadeDevice(const char* port) { m_pixelcadeDevice = port; }
  const char* GetPixelcadeDevice() const { return m_pixelcadeDevice.c_str(); }
  void SetDMDServer(bool dmdServer) { m_dmdServer = dmdServer; }
  bool IsDmdServer() { return m_dmdServer; }
  void SetDMDServerAddr(const char* addr) { m_dmdServerAddr = addr; }
  const char* GetDMDServerAddr() const { return m_dmdServerAddr.c_str(); }
  void SetDMDServerPort(int port) { m_dmdServerPort = port; }
  int GetDMDServerPort() const { return m_dmdServerPort; }
  DMDUtil_LogLevel GetLogLevel() const { return m_logLevel; }
  void SetLogLevel(DMDUtil_LogLevel logLevel) { m_logLevel = logLevel; }
  DMDUtil_LogCallback GetLogCallback() const { return m_logCallback; }
  void SetLogCallback(DMDUtil_LogCallback callback) { m_logCallback = callback; }
  DMDUtil_PUPTriggerCallbackContext GetPUPTriggerCallbackContext() const { return m_pupTriggerCallbackContext; }
  void SetPUPTriggerCallback(DMDUtil_PUPTriggerCallback callback, void* pUserData)
  {
    m_pupTriggerCallbackContext.callback = callback;
    m_pupTriggerCallbackContext.pUserData = pUserData;
  }

 private:
  Config();
  ~Config() {}

  static Config* m_pInstance;
  bool m_altColor;
  std::string m_altColorPath;
  bool m_pupCapture;
  std::string m_pupVideosPath;
  bool m_pupExactColorMatch;
  int m_framesTimeout;
  int m_framesToSkip;
  bool m_zedmd;
  std::string m_zedmdDevice;
  bool m_zedmdDebug;
  int m_zedmdBrightness;
  bool m_zedmdWiFiEnabled;
  std::string m_zedmdWiFiAddr;
  bool m_dmdServer;
  std::string m_dmdServerAddr;
  int m_dmdServerPort;
  bool m_pixelcade;
  std::string m_pixelcadeDevice;
  DMDUtil_LogLevel m_logLevel;
  DMDUtil_LogCallback m_logCallback;
  DMDUtil_PUPTriggerCallbackContext m_pupTriggerCallbackContext;
};

}  // namespace DMDUtil
