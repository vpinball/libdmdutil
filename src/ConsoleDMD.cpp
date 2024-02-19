#include "DMDUtil/ConsoleDMD.h"

#include <stdio.h>

namespace DMDUtil
{

ConsoleDMD::ConsoleDMD(FILE* f) { out = f; }

ConsoleDMD::~ConsoleDMD() {}

void ConsoleDMD::Render(uint8_t* buffer, uint16_t width, uint16_t height, uint8_t bitDepth)
{
  for (uint16_t y = 0; y < height; y++)
  {
    for (uint16_t x = 0; x < width; x++)
    {
      uint8_t value = buffer[y * width + x];
      if (bitDepth > 2)
      {
        fprintf(out, "%2x", value);
      }
      else
      {
        switch (value)
        {
          case 0:
            fprintf(out, "\033[0;40m⚫\033[0m");
            break;
          case 1:
            fprintf(out, "\033[0;40m🟤\033[0m");
            break;
          case 2:
            fprintf(out, "\033[0;40m🟠\033[0m");
            break;
          case 3:
            fprintf(out, "\033[0;40m🟡\033[0m");
            break;
        }
      }
    }
    fprintf(out, "\n");
  }
}
}  // namespace DMDUtil