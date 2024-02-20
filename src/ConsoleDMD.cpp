#include "DMDUtil/ConsoleDMD.h"

#include <cstdio>

namespace DMDUtil
{

ConsoleDMD::ConsoleDMD(bool overwrite, FILE* out)
{
  m_overwrite = overwrite;
  m_out = out;
}

ConsoleDMD::~ConsoleDMD() {}

void ConsoleDMD::Render(uint8_t* buffer, uint16_t width, uint16_t height, uint8_t bitDepth) const
{
  for (uint16_t y = 0; y < height; y++)
  {
    for (uint16_t x = 0; x < width; x++)
    {
      uint8_t value = buffer[y * width + x];
      if (bitDepth > 2)
      {
        fprintf(m_out, "%2x", value);
      }
      else
      {
        switch (value)
        {
          case 0:
            fprintf(m_out, "\033[0;40m⚫\033[0m");
            break;
          case 1:
            fprintf(m_out, "\033[0;40m🟤\033[0m");
            break;
          case 2:
            fprintf(m_out, "\033[0;40m🟠\033[0m");
            break;
          case 3:
            fprintf(m_out, "\033[0;40m🟡\033[0m");
            break;
        }
      }
    }
    fprintf(m_out, "\n");
  }

  if (m_overwrite) fprintf(m_out, "\033[%dA", height);
}
}  // namespace DMDUtil
