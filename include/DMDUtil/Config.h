#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdint>
#include <string>
#include <stdarg.h>

typedef void(DMDUTILCALLBACK *DMDUtil_LogCallback)(const char *format, va_list args);

namespace DMDUtil {

class DMDUTILAPI Config
{
public:
   static Config* GetInstance();
   bool IsAltColor() { return m_altColor; }
   void SetAltColor(bool altColor) { m_altColor = altColor; }
   void SetAltColorPath(const std::string& path) { m_altColorPath = path; }
   bool IsZeDMD() { return m_zedmd; }
   void SetZeDMD(bool zedmd) { m_zedmd = zedmd; }
   const std::string& GetZeDMDDevice() { return m_zedmdDevice; }
   void SetZeDMDDevice(const std::string& port) { m_zedmdDevice = port; }
   const bool IsZeDMDDebug() { return m_zedmdDebug; }
   void SetZeDMDDebug(bool debug) { m_zedmdDebug = debug; }
   const int GetZeDMDRGBOrder() { return m_zedmdRgbOrder; }
   void SetZeDMDRGBOrder(int rgbOrder) { m_zedmdRgbOrder = rgbOrder; }
   const int GetZeDMDBrightness() { return m_zedmdBrightness; }
   void SetZeDMDBrightness(int brightness) { m_zedmdBrightness = brightness; }
   const bool IsZeDMDSaveSettings() { return m_zedmdSaveSettings; }
   void SetZeDMDSaveSettings(bool saveSettings) { m_zedmdSaveSettings = saveSettings; }
   bool IsPixelcade() { return m_pixelcade; }
   void SetPixelcade(bool pixelcade) { m_pixelcade = pixelcade; }
   void SetPixelcadeDevice(const std::string& port) { m_pixelcadeDevice = port; }
   const std::string& GetPixelcadeDevice() { return m_pixelcadeDevice; }
   DMDUtil_LogCallback GetLogCallback() { return m_logCallback; }
   void SetLogCallback(DMDUtil_LogCallback callback) { m_logCallback = callback; }
   const std::string& GetAltColorPath() { return m_altColorPath; }

private:
   Config();
   ~Config();

   static Config* m_pInstance;
   bool m_altColor;
   std::string m_altColorPath;
   bool m_zedmd;
   std::string m_zedmdDevice;
   bool m_zedmdDebug;
   int m_zedmdRgbOrder;
   int m_zedmdBrightness;
   bool m_zedmdSaveSettings;
   bool m_pixelcade;
   std::string m_pixelcadeDevice;
   DMDUtil_LogCallback m_logCallback;
};

}