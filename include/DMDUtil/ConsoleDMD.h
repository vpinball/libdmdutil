#pragma once

#include <cstdint>
#include <cstdio>

namespace DMDUtil
{

class ConsoleDMD
{
 public:
  ConsoleDMD(FILE* f);
  ~ConsoleDMD();

  void Render(uint8_t* buffer, uint16_t width, uint16_t height, uint8_t bitDepth);

 private:
  FILE* out;
};

}  // namespace DMDUtil
