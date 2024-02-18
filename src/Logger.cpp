#include "Logger.h"

#include "DMDUtil/Config.h"

namespace DMDUtil
{

void Log(const char *format, ...)
{
  DMDUtil_LogCallback logCallback = Config::GetInstance()->GetLogCallback();

  if (!logCallback) return;

  va_list args;
  va_start(args, format);
  (*(logCallback))(format, args);
  va_end(args);
}

}  // namespace DMDUtil
