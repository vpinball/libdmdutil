#include "Logger.h"

#include "DMDUtil/Config.h"

namespace DMDUtil
{

void Log(DMDUtil_LogLevel logLevel, const char* format, ...)
{
  static Config* pConfig = pConfig->GetInstance();

  DMDUtil_LogCallback logCallback = pConfig->GetLogCallback();

  if (!logCallback || logLevel < pConfig->GetLogLevel()) return;

  va_list args;
  va_start(args, format);
  (*(logCallback))(logLevel, format, args);
  va_end(args);
}

}  // namespace DMDUtil
