#pragma once

#include <cstdint>
#include <cstdio>

namespace DMDUtil
{

class ConsoleDMD
{
 public:
  ConsoleDMD(bool overwrite, FILE* out);
  ~ConsoleDMD();

  void Render(uint8_t* buffer, uint16_t width, uint16_t height, uint8_t bitDepth);

 private:
  bool m_overwrite;
  FILE* m_out;
};

}  // namespace DMDUtil
